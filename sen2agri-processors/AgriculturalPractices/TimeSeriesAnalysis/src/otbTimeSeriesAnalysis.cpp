/*=========================================================================
  *
  * Program:      Sen2agri-Processors
  * Language:     C++
  * Copyright:    2015-2016, CS Romania, office@c-s.ro
  * See COPYRIGHT file for details.
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.

 =========================================================================*/
#include "otbWrapperApplication.h"
#include "otbWrapperApplicationFactory.h"
#include <boost/filesystem.hpp>

#include "otbOGRDataSourceWrapper.h"

#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/fusion/algorithm/iteration/for_each.hpp>
#include <boost/accumulators/statistics.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include "TimeSeriesAnalysisTypes.h"
#include "TimeSeriesAnalysisUtils.h"
#include "StatisticsInfosReaderFactory.h"
#include "../../Common/include/PracticeReaderFactory.h"

#include "TsaPlotsWriter.h"
#include "TsaCSVWriter.h"
#include "TsaContinuousFileWriter.h"
#include "TsaDebugPrinter.h"
#include "TsaPrevPrdReader.h"

//#define INPUT_SHP_DATE_PATTERN      "%4d-%2d-%2d"

#define CATCH_CROP_VAL                  "CatchCrop"
#define CATCH_CROP_IS_MAIN_VAL          "CatchCropIsMain"
#define FALLOW_LAND_VAL                 "Fallow"
#define NITROGEN_FIXING_CROP_VAL        "NFC"

#define MIN_REQUIRED_COHE_VALUES        26
#define SEC_IN_5_WEEKS                  3024000 // 5 weeks * 7 days * seconds in day

// TODO : re-analyse the usage of dates (start of week or exact date)

template <typename T>
bool InputFileLineInfoEqComparator (const InputFileLineInfoType& struct1, const InputFileLineInfoType& struct2) {
  return (struct1.weekNo == struct2.weekNo);
}

namespace otb
{
namespace Wrapper
{
class TimeSeriesAnalysis : public Application
{
public:
    /** Standard class typedefs. */
    typedef TimeSeriesAnalysis        Self;
    typedef Application                   Superclass;
    typedef itk::SmartPointer<Self>       Pointer;
    typedef itk::SmartPointer<const Self> ConstPointer;

    /** Standard macro */
    itkNewMacro(Self);

    itkTypeMacro(TimeSeriesAnalysis, otb::Application);

    /** Filters typedef */

private:
    TimeSeriesAnalysis()
    {
        m_countryName = "UNKNOWN";
        m_practiceName = NA_STR;
        m_year = "UNKNOWN";
        m_bAllowGaps = true;
        m_bGapsFill = false;
        m_nMinS1PixCnt = 8;

        m_OpticalThrVegCycle = 550; //350;

        // for MARKER 2 - NDVI loss
        // expected value of harvest/clearance
        m_NdviDown = 300;// 350;
        // buffer value (helps in case of sparse ndvi time-series)
        m_NdviUp = 400;//550;
        // opt.thr.value is round up to ndvi.step
        m_ndviStep = 5;
        m_OpticalThresholdMinimum = 100;

        // for MARKER 5 - COHERENCE increase
        m_CohThrBase = 0.1; //0.05;
        m_CohThrHigh = 0.2; //0.15;
        m_CohThrAbs = 0.7;  //0.75;

        // for MARKER 3 - BACKSCATTER loss
        m_AmpThrMinimum = 0.1;

        // INPUT THRESHOLDS - EFA PRACTICE evaluation
        // TODO: Ask Gisat for CZE
        m_CatchCropIsMain = CATCH_CROP_IS_MAIN_VAL;
        m_CatchPeriod = 56;               // in days (e.g. 8 weeks == 56 days)
        m_CatchProportion = 1./3;       // buffer threshold

        m_EfaNdviThr = 400; // 325;
        m_EfaNdviUp = 600; // 400;
        m_EfaNdviDown = 600; // 300;

        m_EfaCohChange = 0.2;
        m_EfaCohValue = 0.7;

        // TODO: ask Gisat about this value
        m_EfaNdviMin = NOT_AVAILABLE;
        m_EfaAmpThr = NOT_AVAILABLE;

        m_UseStdDevInAmpThrValComp = false;
        m_OpticalThrBufDenominator = 6;
        m_AmpThrBreakDenominator = 6;
        m_AmpThrValDenominator = 2;

        m_CatchCropVal = CATCH_CROP_VAL;
        m_FallowLandVal = FALLOW_LAND_VAL;
        m_NitrogenFixingCropVal = NITROGEN_FIXING_CROP_VAL;

        m_bVerbose = false;

        time(&m_ttLimitAcqDate);
        m_ttLimitAcqDate -= (SEC_IN_WEEK * 2);
    }

    void DoInit() override
    {
        SetName("TimeSeriesAnalysis");
        SetDescription("TODO.");

        // Documentation
        SetDocName("TODO");
        SetDocLongDescription("TODO");
        SetDocLimitations("None");
        SetDocAuthors("CIU");
        SetDocSeeAlso(" ");

        AddDocTag(Tags::Learning);

        AddParameter(ParameterType_String, "dircohe", "Coherence statistics files directory");
        SetParameterDescription("dircohe", "Coherence statistics files directory");

        AddParameter(ParameterType_String, "diramp", "Amplitude statistics files directory");
        SetParameterDescription("diramp", "Amplitude statistics files directory");

        AddParameter(ParameterType_String, "dirndvi", "NDVI statistics files directory");
        SetParameterDescription("dirndvi", "NDVI statistics files directory");

        AddParameter(ParameterType_String, "harvestshp", "Harvest information shapefile");
        SetParameterDescription("harvestshp", "Harvest information shapefile");

        AddParameter(ParameterType_String, "outdir", "Output folder directory");
        SetParameterDescription("outdir", "Output folder directory");

        AddParameter(ParameterType_String, "intype", "Type of the inputs");
        SetParameterDescription("intype", "If xml then an xml file is expected as input. "
                                          "If dir, the application expects a directory with txt files for each parcel");
        MandatoryOff("intype");

        AddParameter(ParameterType_Int, "allowgaps", "Allow week gaps in time series");
        SetParameterDescription("allowgaps", "Allow week gaps in  time series");
        SetDefaultParameterInt("allowgaps", 1);
        MandatoryOff("allowgaps");

        AddParameter(ParameterType_Int, "gapsfill", "Perform gaps filling when gaps are allowed");
        SetParameterDescription("gapsfill", "Perform gaps filling when gaps are allowed");
        SetDefaultParameterInt("gapsfill", 0);
        MandatoryOff("gapsfill");

        AddParameter(ParameterType_String, "country", "The country to be used in the output file name");
        SetParameterDescription("country", "The country to be used in the output file name");
        MandatoryOff("country");

        AddParameter(ParameterType_String, "practice", "The practice to be used in the output file name");
        SetParameterDescription("practice", "The practice to be used in the output file name");
        MandatoryOff("practice");

        AddParameter(ParameterType_String, "year", "The year to be used in the output file name");
        SetParameterDescription("practice", "The year to be used in the output file name");
        MandatoryOff("year");


// /////////////////////// CONFIGURABLE PARAMETERS PER SITE AND PRACTICE ////////////////////
        AddParameter(ParameterType_Float, "optthrvegcycle", "Optical threshold vegetation cycle");
        SetParameterDescription("optthrvegcycle", "Optical threshold vegetation cycle");
//        SetDefaultParameterFloat("optthrvegcycle", 550);                                        // TODO: changing
//        MandatoryOff("optthrvegcycle");

        // for MARKER 2 - NDVI loss
        // expected value of harvest/clearance
        AddParameter(ParameterType_Float, "ndvidw", "NDVI down");
        SetParameterDescription("ndvidw", "for MARKER 2 - NDVI loss - expected value of harvest/clearance");
//        SetDefaultParameterFloat("ndvidw", 350);                                        // TODO: changing
//        MandatoryOff("ndvidw");

        AddParameter(ParameterType_Float, "ndviup", "NDVI up");
        SetParameterDescription("ndviup", "buffer value (helps in case of sparse ndvi time-serie)");
//        SetDefaultParameterFloat("ndviup", 550);                                        // TODO: changing
//        MandatoryOff("ndviup");

        AddParameter(ParameterType_Float, "ndvistep", "NDVI step");
        SetParameterDescription("ndvistep", "opt.thr.value is round up to ndvi.step");
//        SetDefaultParameterFloat("ndvistep", 5);
//        MandatoryOff("ndvistep");

        AddParameter(ParameterType_Float, "optthrmin", "Optical threshold minimum");
        SetParameterDescription("optthrmin", "opt.thr.value is round up to ndvi.step");
//        SetDefaultParameterFloat("optthrmin", 100);
//        MandatoryOff("optthrmin");

        AddParameter(ParameterType_Float, "cohthrbase", "COHERENCE increase");
        SetParameterDescription("cohthrbase", "for MARKER 5 - COHERENCE increase");
//        SetDefaultParameterFloat("cohthrbase", 0.05);                                   // TODO: changing
//        MandatoryOff("cohthrbase");

        AddParameter(ParameterType_Float, "cohthrhigh", "Coherence threshold high");
        SetParameterDescription("cohthrhigh", "for MARKER 5 - COHERENCE increase");
//        SetDefaultParameterFloat("cohthrhigh", 0.15);                                   // TODO: changing
//        MandatoryOff("cohthrhigh");

        AddParameter(ParameterType_Float, "cohthrabs", "Coherence threshold absolute");
        SetParameterDescription("cohthrabs", "for MARKER 5 - COHERENCE increase");
//        SetDefaultParameterFloat("cohthrabs", 0.75);                                   // TODO: changing
//        MandatoryOff("cohthrabs");

        AddParameter(ParameterType_Float, "ampthrmin", "BACKSCATTER loss");
        SetParameterDescription("ampthrmin", "for MARKER 3 - BACKSCATTER loss");
//        SetDefaultParameterFloat("ampthrmin", 0.1);                                   // TODO: changing
//        MandatoryOff("ampthrmin");

        // INPUT THRESHOLDS - EFA PRACTICE evaluation
        AddParameter(ParameterType_String, "catchmain", "Catch main");
        SetParameterDescription("catchmain", "TODO");
        MandatoryOff("catchmain");

        AddParameter(ParameterType_String, "catchcropismain", "Catch crop is main");
        SetParameterDescription("catchcropismain", "TODO");
        MandatoryOff("catchcropismain");

        AddParameter(ParameterType_Int, "catchperiod", "Catch period");
        SetParameterDescription("catchperiod", "in days (e.g. 8 weeks == 56 days)");
        SetDefaultParameterInt("catchperiod", 56);
        MandatoryOff("catchperiod");

        AddParameter(ParameterType_Float, "catchproportion", "Catch proportion");
        SetParameterDescription("catchproportion", "buffer threshold");
        SetDefaultParameterFloat("catchproportion", 1./3);
        MandatoryOff("catchproportion");

        AddParameter(ParameterType_String, "catchperiodstart", "Catch period start");
        SetParameterDescription("catchperiodstart", "Catch period start");
        MandatoryOff("catchperiodstart");

        AddParameter(ParameterType_Int, "efandvithr", "Efa NDVI threshold");
        SetParameterDescription("efandvithr", "EFA practices NDVI threshold");
        SetDefaultParameterInt("efandvithr", 325);                // TODO: changing
        MandatoryOff("efandvithr");

        AddParameter(ParameterType_Int, "efandviup", "Efa NDVI up");
        SetParameterDescription("efandviup", "EFA practices NDVI up");
        SetDefaultParameterInt("efandviup", 400);                // TODO: changing
        MandatoryOff("efandviup");

        AddParameter(ParameterType_Int, "efandvidw", "Efa NDVI down");
        SetParameterDescription("efandvidw", "EFA practices NDVI down");
        SetDefaultParameterInt("efandvidw", 300);                // TODO: changing
        MandatoryOff("efandvidw");

        AddParameter(ParameterType_Float, "efacohchange", "Efa Coherence change");
        SetParameterDescription("efacohchange", "EFA practices coherence change");
        SetDefaultParameterInt("efacohchange", 0.2);
        MandatoryOff("efacohchange");

        AddParameter(ParameterType_Float, "efacohvalue", "Efa Coherence value");
        SetParameterDescription("efacohvalue", "EFA practices coherence value");
        SetDefaultParameterInt("efacohvalue", 0.7);
        MandatoryOff("efacohvalue");

        AddParameter(ParameterType_Float, "efandvimin", "Efa NDVI minimum");
        SetParameterDescription("efandvimin", "EFA practices NDVI minimum");
        SetDefaultParameterInt("efandvimin", NOT_AVAILABLE);
        MandatoryOff("efandvimin");

        AddParameter(ParameterType_Float, "efaampthr", "Efa Amplitude threshold");
        SetParameterDescription("efaampthr", "EFA practices amplitude threshold");
        SetDefaultParameterInt("efaampthr", NOT_AVAILABLE);
        MandatoryOff("efaampthr");

        AddParameter(ParameterType_Int, "stddevinampthr", "Use Standard deviation in amplitude threshold value computation");
        SetParameterDescription("stddevinampthr", "Use Standard deviation in amplitude threshold value computation");
        SetDefaultParameterInt("stddevinampthr", 0);
        MandatoryOff("stddevinampthr");

        AddParameter(ParameterType_Int, "optthrbufden", "Optical threshold buffer denomimator");
        SetParameterDescription("optthrbufden", "Optical threshold buffer denomimator");
        SetDefaultParameterInt("optthrbufden", 6);
        MandatoryOff("optthrbufden");

        AddParameter(ParameterType_Int, "ampthrbreakden", "Amplitude threshold break denomimator");
        SetParameterDescription("ampthrbreakden", "Amplitude threshold break denomimator");
        SetDefaultParameterInt("ampthrbreakden", 6);
        MandatoryOff("ampthrbreakden");

        AddParameter(ParameterType_Int, "ampthrvalden", "Amplitude threshold value denomimator");
        SetParameterDescription("ampthrvalden", "Amplitude threshold value denomimator");
        SetDefaultParameterInt("ampthrvalden", 2);
        MandatoryOff("ampthrvalden");

        AddParameter(ParameterType_String, "flmarkstartdate", "Fallow land markers start date");
        SetParameterDescription("flmarkstartdate", "Fallow land markers start date");
        MandatoryOff("flmarkstartdate");

        AddParameter(ParameterType_String, "flmarkstenddate", "Fallow land markers end date");
        SetParameterDescription("flmarkstenddate", "Fallow land markers end date");
        MandatoryOff("flmarkstenddate");

        AddParameter(ParameterType_Int, "s1pixthr", "Number of minimum S1 pixels to consider a parcel valid");
        SetParameterDescription("s1pixthr", "Number of minimum S1 pixels to consider a parcel valid");
        MandatoryOff("s1pixthr");
        SetDefaultParameterInt("s1pixthr", 1);


        AddParameter(ParameterType_String, "catchcropval", "Catch crop value");
        SetParameterDescription("catchcropval", "Catch crop value");
        MandatoryOff("catchcropval");

        AddParameter(ParameterType_String, "flval", "Fallow land value");
        SetParameterDescription("flval", "Fallow land value");
        MandatoryOff("flval");

        AddParameter(ParameterType_String, "ncval", "Nitrogen fixing crop value");
        SetParameterDescription("ncval", "Nitrogen fixing crop value");
        MandatoryOff("ncval");

        AddParameter(ParameterType_Int, "plotgraph", "Plot output graphs");
        SetParameterDescription("plotgraph", "In case graphs shall be generated set the value to TRUE otherwise set to FALSE");
        SetDefaultParameterInt("plotgraph", 0);
        MandatoryOff("plotgraph");

        AddParameter(ParameterType_Int, "rescontprd", "Results continuous products");
        SetParameterDescription("rescontprd", "In case continuos products (csv file) shall be generated set the value to TRUE otherwise set to FALSE");
        SetDefaultParameterInt("rescontprd", 0);
        MandatoryOff("rescontprd");

        AddParameter(ParameterType_Int, "debug", "Print debug messages");
        SetParameterDescription("debug", "Print debug messages");
        SetDefaultParameterInt("debug", 1);
        MandatoryOff("debug");

        AddParameter(ParameterType_String, "prevprd", "The previous product");
        SetParameterDescription("prevprd", "The previous product of this practice (if any) to be used for extracting "
                                           "the H_WEEK that needs to be preserved");
        MandatoryOff("prevprd");

        AddParameter(ParameterType_Int, "minacqs", "The minimum numner of coherence acquision dates in the time series");
        SetParameterDescription("minacqs", "The minimum numner of coherence acquision dates in the time series");
        SetDefaultParameterInt("minacqs", MIN_REQUIRED_COHE_VALUES);
        MandatoryOff("minacqs");

        AddParameter(ParameterType_String, "acqsdatelimit", "Limit acquisition date");
        SetParameterDescription("acqsdatelimit", "Limit acquisition date");
        MandatoryOff("acqsdatelimit");

        // Doc example parameter settings
        //SetDocExampleParameterValue("in", "support_image.tif");
    }

    void DoUpdateParameters() override
    {
    }

    void ExtractParameters() {
        m_outputDir = trim(GetParameterAsString("outdir"));
        m_bAllowGaps = GetParameterInt("allowgaps") != 0;
        m_bGapsFill = GetParameterInt("gapsfill") != 0;
        m_debugPrinter.SetDebugMode(GetParameterInt("debug") != 0);
        m_nMinS1PixCnt = GetParameterInt("s1pixthr");

        if (HasValue("country")) {
            m_countryName = trim(GetParameterAsString("country"));
        }
        if (HasValue("practice")) {
            m_practiceName = trim(GetParameterAsString("practice"));
        }
        if (HasValue("year")) {
            m_year = trim(GetParameterAsString("year"));
        }
        if (HasValue("catchcropval")) {
            m_CatchCropVal = GetParameterString("catchcropval");
        }
        if (HasValue("flval")) {
            m_FallowLandVal = GetParameterString("flval");
        }
        if (HasValue("ncval")) {
            m_NitrogenFixingCropVal = GetParameterString("ncval");
        }

//  ///////////////////////////////////////////////////////////////////
        if (HasValue("optthrvegcycle")) {
            m_OpticalThrVegCycle = GetParameterFloat("optthrvegcycle");
        }
        if (HasValue("ndvidw")) {
            m_NdviDown = GetParameterFloat("ndvidw");
        }
        if (HasValue("ndviup")) {
            m_NdviUp = GetParameterFloat("ndviup");
        }
        if (HasValue("ndvistep")) {
            m_ndviStep = GetParameterFloat("ndvistep");
        }
        if (HasValue("optthrmin")) {
            m_OpticalThresholdMinimum = GetParameterFloat("optthrmin");
        }
        if (HasValue("cohthrbase")) {
            m_CohThrBase = GetParameterFloat("cohthrbase");
        }
        if (HasValue("cohthrhigh")) {
            m_CohThrHigh = GetParameterFloat("cohthrhigh");
        }
        if (HasValue("cohthrabs")) {
            m_CohThrAbs = GetParameterFloat("cohthrabs");
        }
        if (HasValue("ampthrmin")) {
            m_AmpThrMinimum = GetParameterFloat("ampthrmin");
        }
        if (HasValue("catchmain")) {
            m_CatchMain = GetParameterString("catchmain");
        }
        if (HasValue("catchcropismain")) {
            m_CatchCropIsMain = GetParameterString("catchcropismain");
        }

        if (HasValue("catchperiod")) {
            m_CatchPeriod = GetParameterInt("catchperiod");
        }
        if (HasValue("catchproportion")) {
            m_CatchProportion = GetParameterFloat("catchproportion");
        }
        if (HasValue("catchperiodstart")) {
            m_CatchPeriodStart = GetParameterString("catchperiodstart");
        }
        if (HasValue("efandvithr")) {
            m_EfaNdviThr = GetParameterInt("efandvithr");
        }
        if (HasValue("efandviup")) {
            m_EfaNdviUp = GetParameterInt("efandviup");
        }
        if (HasValue("efandvidw")) {
            m_EfaNdviDown = GetParameterInt("efandvidw");
        }
        if (HasValue("efacohchange")) {
            m_EfaCohChange = GetParameterFloat("efacohchange");
        }
        if (HasValue("efacohvalue")) {
            m_EfaCohValue = GetParameterFloat("efacohvalue");
        }
        if (HasValue("efandvimin")) {
            m_EfaNdviMin = GetParameterFloat("efandvimin");
        }
        if (HasValue("efaampthr")) {
            m_EfaAmpThr = GetParameterFloat("efaampthr");
        }
        if (HasValue("stddevinampthr")) {
            m_UseStdDevInAmpThrValComp = GetParameterInt("stddevinampthr");
        }
        if (HasValue("optthrbufden")) {
            m_OpticalThrBufDenominator = GetParameterInt("optthrbufden");
        }
        if (HasValue("ampthrbreakden")) {
            m_AmpThrBreakDenominator = GetParameterInt("ampthrbreakden");
        }
        if (HasValue("ampthrvalden")) {
            m_AmpThrValDenominator = GetParameterInt("ampthrvalden");
        }
        if (HasValue("flmarkstartdate")) {
            m_flMarkersStartDateStr = GetParameterString("flmarkstartdate");
        }
        if (HasValue("flmarkstenddate")) {
            m_flMarkersEndDateStr = GetParameterString("flmarkstenddate");
        }
        if (HasValue("plotgraph")) {
            m_plotsWriter.SetEnabled(GetParameterInt("plotgraph") != 0);
        }
        if (HasValue("rescontprd")) {
            m_contFileWriter.SetEnabled(GetParameterInt("rescontprd") != 0);
        }

        if (HasValue("prevprd")) {
            const std::string &prevFileName = TsaCSVWriter::BuildResultsCsvFileName(m_practiceName,
                                                                               m_countryName,
                                                                               std::atoi(m_year.c_str()));
            const std::string &prevPrdDir = GetParameterString("prevprd");
            boost::filesystem::path rootFolder(prevPrdDir);
            m_prevPrdReader.Initialize((rootFolder / prevFileName).string());
        }

        if (HasValue("acqsdatelimit")) {
            const std::string &limitDateStr = GetParameterString("acqsdatelimit");
            m_ttLimitAcqDate = GetTimeFromString(limitDateStr);
            time_t yearLimitDate = GetTimeFromString(m_year + "-12-31");
            if (m_ttLimitAcqDate > yearLimitDate) {
                m_ttLimitAcqDate = yearLimitDate;
            }
        }

        if (m_practiceName == m_CatchCropVal) {
            if (m_CatchMain.size() == 0) {
                itkExceptionMacro("catch main parameter was not specified for the Catch Crop practice!");
            }
        }
    }
    void DoExecute() override
    {
        // Extract the parameters
        ExtractParameters();

        std::string inputType = "csv";
        if (HasValue("intype")) {
            const std::string &inType = GetParameterAsString("intype");
            if (inType == "dir" || inType == "xml" || inType == "csv") {
                inputType = inType;
            } else {
                itkExceptionMacro("Invalid value provided for parameter intype " << inType
                                  << ". Only dir or xml or csv are supported (default: csv)");
            }
        }

        int curYear = std::atoi(m_year.c_str());
        auto factory = StatisticsInfosReaderFactory::New();
        m_pAmpReader = factory->GetInfosReader(inputType);
        m_pAmpReader->Initialize(GetParameterAsString("diramp"), {"VV", "VH"}, curYear);
        m_pAmpReader->SetUseDate2(false);
        m_pAmpReader->SetSwitchDates(false);

        m_pNdviReader = factory->GetInfosReader(inputType);
        m_pNdviReader->Initialize(GetParameterAsString("dirndvi"), {}, curYear);
        m_pNdviReader->SetUseDate2(false);
        m_pNdviReader->SetSwitchDates(false);

        m_pCoheReader = factory->GetInfosReader(inputType);
        m_pCoheReader->Initialize(GetParameterAsString("dircohe"), {"VV", "VH"}, curYear);
        m_pCoheReader->SetMinRequiredEntries(GetParameterInt("minacqs"));

        m_pCoheReader->SetUseDate2(true);
        m_pCoheReader->SetSwitchDates(true);


        const std::string &practicesInfoFile = GetParameterAsString("harvestshp");
        boost::filesystem::path practicesInfoPath(practicesInfoFile);
        std::string pfFormat = practicesInfoPath.extension().c_str();
        pfFormat.erase(pfFormat.begin(), std::find_if(pfFormat.begin(), pfFormat.end(), [](int ch) {
                return ch != '.';
            }));

        auto practiceReadersFactory = PracticeReaderFactory::New();
        m_pPracticeReader = practiceReadersFactory->GetPracticeReader(pfFormat);
        m_pPracticeReader->SetSource(practicesInfoFile);

        // write first the CSV header
        if (!m_csvWriter.WriteCSVHeader(m_outputDir, m_practiceName, m_countryName, curYear)) {
            otbAppLogFATAL("Error opening output file for practice " << m_practiceName << " and year "
                           << curYear << " in the directory " << m_outputDir << ". Exiting...");
        }

        // create the plots file
        m_plotsWriter.CreatePlotsFile(m_outputDir, m_practiceName, m_countryName, curYear);

        // create the continous products file
        m_contFileWriter.CreateContinousProductFile(m_outputDir, m_practiceName, m_countryName, curYear);

        // start processing features
        using namespace std::placeholders;
        std::function<bool(const FeatureDescription&, void*)> f = std::bind(&TimeSeriesAnalysis::HandleFeature, this, _1, _2);
        m_pPracticeReader->ExtractFeatures(f);

        // close the plots file
        m_plotsWriter.ClosePlotsFile();

        otbAppLogINFO("Execution DONE!");
    }

    bool HandleFeature(const FeatureDescription& feature, void*) {
        // DisplayFeature(feature);

        const std::string &fieldId = feature.GetFieldId();
        const std::string &vegetationStart = feature.GetVegetationStart();
        const std::string &harvestStart = feature.GetHarvestStart();
        const std::string &harvestEnd = feature.GetHarvestEnd();
        const std::string &practiceStart = feature.GetPracticeStart();
        const std::string &practiceEnd = feature.GetPracticeEnd();

        // TODO: Remove this after testing
//        if (fieldId != "681116204" && fieldId != "680105602" && fieldId != "584109910/2" && fieldId != "655115201_1")
//        {
//            return false;
//        }
//        if (fieldId != "613102006_21")
//        {
//            return false;
//        }

//        if (fieldId != "670104903") {
//            return false;
//        }
//        if (fieldId != "663101501_6") {
//            return false;
//        }
//        if (fieldId != "681105603") {
//            return false;
//        }

//        if (fieldId != "636105803_2") {
//            return false;
//        }

//        if (fieldId != "636102704_17") {
//            return false;
//        }

//        if (feature.GetFieldSeqId() != "10018") {
//            return false;
//        }

        FieldInfoType fieldInfos(fieldId);

        fieldInfos.fieldSeqId = feature.GetFieldSeqId();
        fieldInfos.ttVegStartTime = GetTimeFromString(vegetationStart);
        fieldInfos.ttVegStartWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttVegStartTime);
        fieldInfos.ttHarvestStartTime = GetTimeFromString(harvestStart);
        fieldInfos.ttHarvestStartWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttHarvestStartTime);
        fieldInfos.ttHarvestEndTime = GetTimeFromString(harvestEnd);
        fieldInfos.ttHarvestEndWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttHarvestEndTime);
        fieldInfos.ttPracticeStartTime = GetTimeFromString(practiceStart);
        fieldInfos.ttPracticeStartWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttPracticeStartTime);
        fieldInfos.ttPracticeEndTime = GetTimeFromString(practiceEnd);
        fieldInfos.ttPracticeEndWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttPracticeEndTime);
        fieldInfos.practiceName = feature.GetPractice();
        fieldInfos.countryCode = feature.GetCountryCode();
        fieldInfos.mainCrop = feature.GetMainCrop();
        fieldInfos.practiceType = feature.GetPracticeType();
        fieldInfos.s1PixValue = feature.GetS1Pix();
        int s1PixVal = std::atoi(fieldInfos.s1PixValue.c_str());
        if (s1PixVal == 0) {
            // set also the fieldInfos.s1PixValue to "0"
            fieldInfos.s1PixValue = std::to_string(s1PixVal);
        }

        int year;
        if (!GetWeekFromDate(vegetationStart, year, fieldInfos.vegStartWeekNo))
        {
            otbAppLogWARNING("Cannot extract vegetation start week from the date " <<
                             vegetationStart << " of the feature " << fieldId);
            return false;
        }
        fieldInfos.year = year;
        if (!GetWeekFromDate(harvestStart, year, fieldInfos.harvestStartWeekNo))
        {
            otbAppLogWARNING("Cannot extract harvest start week from the date " <<
                             harvestStart << " of the feature " << fieldId);
            return false;
        }
        if (year != fieldInfos.year) {
            otbAppLogWARNING("Vegetation year and harvest start year are different for field " << fieldId);
        }
        if (year != fieldInfos.year) {
            otbAppLogWARNING("Vegetation year and harvest start year are different for field " << fieldId);
        }

        if (fieldInfos.vegStartWeekNo == 1) {
            // increment the vegetation start time to the next week
            fieldInfos.ttVegStartWeekFloorTime += SEC_IN_WEEK;
            fieldInfos.vegStartWeekNo++;
        }
        if (fieldInfos.harvestStartWeekNo == 1) {
            // increment the harvest start time to the next week
            fieldInfos.ttHarvestStartWeekFloorTime += SEC_IN_WEEK;
            fieldInfos.harvestStartWeekNo++;
        }

//      DEBUG
        m_debugPrinter.PrintFieldGeneralInfos(fieldInfos);
//      DEBUG

        bool bOK = true;
        int harvestStatusInitVal = NOT_AVAILABLE;
        if (s1PixVal < m_nMinS1PixCnt) {
            bOK = false;
            harvestStatusInitVal = NOT_AVAILABLE_1;
        }

        if (m_bVerbose) {
            otbAppLogINFO("Extracting amplitude file infos for field  " << fieldId);
        }
        if (bOK && !ExtractAmplitudeFilesInfos(fieldInfos)) {
            bOK = false;
        }

        if (m_bVerbose) {
            otbAppLogINFO("Extracting coherence file infos for field  " << fieldId);
        }
        if (bOK && !ExtractCoherenceFilesInfos(fieldInfos)) {
            bOK = false;
        }

        if (m_bVerbose) {
            otbAppLogINFO("Extracting NDVI file infos for field  " << fieldId);
        }
        if (bOK && !ExtractNdviFilesInfos(fieldInfos)) {
            bOK = false;
        }

        if (m_bVerbose) {
            otbAppLogINFO("Processing infos for field  " << fieldId);
        }
        if (bOK && !ProcessFieldInformation(fieldInfos)) {
            bOK = false;
        }
        if (!bOK) {
            fieldInfos.gapsInfos = harvestStatusInitVal;
            fieldInfos.hS1GapsInfos = harvestStatusInitVal;
            fieldInfos.h_W_S1GapsInfos = harvestStatusInitVal;
            fieldInfos.pS1GapsInfos = harvestStatusInitVal;

            // in case an error occurred, write in the end the parcel but with invalid infos
            HarvestEvaluationInfoType harvestEvalInfos(harvestStatusInitVal);

            harvestEvalInfos.Initialize(fieldInfos);
            m_csvWriter.WriteHarvestInfoToCsv(fieldInfos, harvestEvalInfos, harvestEvalInfos);
        }

        return bOK;
    }

    bool ExtractAmplitudeFilesInfos(FieldInfoType &fieldInfo)
    {
        std::map<std::string, std::vector<InputFileLineInfoType>> mapInfos;
        if (!m_pAmpReader->GetEntriesForField(fieldInfo.fieldSeqId, {"VV", "VH"}, mapInfos)) {
        //if (!m_pAmpReader->GetEntriesForField(fieldInfo.fieldId, {"VV", "VH"}, mapInfos)) {
            otbAppLogWARNING("Cannot extract amplitude infos for the field " << fieldInfo.fieldId);
            return false;
        }
        std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVV;
        std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVH;
        itVV = mapInfos.find("VV");
        itVH = mapInfos.find("VH");
        if (itVV == mapInfos.end() || itVH == mapInfos.end() ||
                itVV->second.size() == 0 || itVH->second.size() == 0) {
            if (m_bVerbose) {
                otbAppLogWARNING("Didn't find entries for both VV and VH amplitude files for the field " << fieldInfo.fieldId);
            }
            return false;
        }

        if (m_bVerbose) {
            otbAppLogINFO("Amplitude : Extracted a number of " << itVV->second.size() <<
                          " VV entries and a number of " << itVH->second.size() << " VH entries!");
        }
        const std::vector<InputFileLineInfoType> &uniqueVVEntries = FilterDuplicates(itVV->second);
        const std::vector<InputFileLineInfoType> &uniqueVHEntries = FilterDuplicates(itVH->second);

        std::vector<InputFileLineInfoType> commonVVInfos;
        std::vector<InputFileLineInfoType> commonVHInfos;
        KeepCommonDates(uniqueVVEntries, uniqueVHEntries, commonVVInfos, commonVHInfos);

        fieldInfo.ampVVLines.insert(fieldInfo.ampVVLines.end(), commonVVInfos.begin(), commonVVInfos.end());
        fieldInfo.ampVHLines.insert(fieldInfo.ampVHLines.end(), commonVHInfos.begin(), commonVHInfos.end());

        if (m_bVerbose) {
            otbAppLogINFO("Amplitude : Available " << fieldInfo.ampVVLines.size() <<
                          " VV entries and a number of " << fieldInfo.ampVHLines.size() << " VH entries!");
        }

        if (!m_bAllowGaps) {
            if (!CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.ampVVLines) ||
                !CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.ampVHLines)) {
                otbAppLogWARNING("No valid amplitude lines were found for the field id (gaps)" << fieldInfo.fieldId);
                return false;
            }
        }

        return true;
    }

    bool ExtractCoherenceFilesInfos(FieldInfoType &fieldInfo)
    {
        std::map<std::string, std::vector<InputFileLineInfoType>> mapInfos;
        //if (!m_pCoheReader->GetEntriesForField(fieldInfo.fieldId, /*{"VV", "VH"}*/{"VV"}, mapInfos)) {
        if (!m_pCoheReader->GetEntriesForField(fieldInfo.fieldSeqId, /*{"VV", "VH"}*/{"VV"}, mapInfos)) {
            otbAppLogWARNING("Cannot extract coherence infos for the field " << fieldInfo.fieldId);
            return false;
        }
        std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVV;
        //std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVH;
        itVV = mapInfos.find("VV");
        //itVH = mapInfos.find("VH");
        if (itVV == mapInfos.end() /*|| itVH == mapInfos.end()*/ ||
            itVV->second.size() == 0 /*|| itVH->second.size() == 0*/) {
            otbAppLogWARNING("Didn't find entries for both VV and VH coherence files for the field " << fieldInfo.fieldId);
            return false;
        }

//        otbAppLogINFO("Coherence : Extracted a number of " << itVV->second.size() <<
//                      " VV entries and a number of " << itVH->second.size() << " VH entries!");
        if (m_bVerbose) {
            otbAppLogINFO("Coherence : Extracted a number of VV entries of " << itVV->second.size());
        }

        const std::vector<InputFileLineInfoType> &uniqueEntries = FilterDuplicates(itVV->second);
        fieldInfo.coheVVLines.insert(fieldInfo.coheVVLines.end(), uniqueEntries.begin(), uniqueEntries.end());
        //fieldInfo.coheVHLines.insert(fieldInfo.coheVHLines.end(), itVH->second.begin(), itVH->second.end());

        if (fieldInfo.coheVVLines.size() <= MIN_REQUIRED_COHE_VALUES /*|| fieldInfo.coheVHLines.size() <= MIN_REQUIRED_COHE_VALUES*/) {
            otbAppLogWARNING("No/empty/short coherence input text files the field id " << fieldInfo.fieldId);
            return false;
        }

        std::sort (fieldInfo.coheVVLines.begin(), fieldInfo.coheVVLines.end(), TimedValInfosComparator<InputFileLineInfoType>());

        for (std::vector<InputFileLineInfoType>::const_iterator it = fieldInfo.coheVVLines.begin() ;
             it != fieldInfo.coheVVLines.end(); ++it) {
            if (IsNA(fieldInfo.coheVVMaxValue)) {
                fieldInfo.coheVVMaxValue = it->meanVal;
            } else {
                if (IsLess(fieldInfo.coheVVMaxValue, it->meanVal)) {
                    fieldInfo.coheVVMaxValue = it->meanVal;
                }
            }
        }

        if (!m_bAllowGaps) {
            if (!CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.coheVVLines) /*||
                !CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.coheVHLines)*/) {
                otbAppLogWARNING("No valid coherence lines were found for the field id (gaps) " << fieldInfo.fieldId);
                return false;
            }
        }

        return true;
    }

    bool ExtractNdviFilesInfos(FieldInfoType &fieldInfo)
    {
        //if (!m_pNdviReader->GetEntriesForField(fieldInfo.fieldId, fieldInfo.ndviLines)) {
        if (!m_pNdviReader->GetEntriesForField(fieldInfo.fieldSeqId, fieldInfo.ndviLines)) {
            otbAppLogWARNING("Cannot extract NDVI infos for the field " << fieldInfo.fieldId);
            return false;
        }
        if (fieldInfo.ndviLines.size() == 0) {
            otbAppLogWARNING("No valid lines were extracted for the field id " << fieldInfo.fieldId);
        }
        return true;
    }

    bool CheckWeekGaps(int &vegetationStartWeek, const std::vector<InputFileLineInfoType> &inLineInfos)
    {
        if (inLineInfos.size() == 0) {
            return false;
        }

        // TODO: this is an workaround to have the same results as 2017 prototype
        // TODO: See why if sorting the initial inputs give different results
        std::vector<InputFileLineInfoType> lineInfos;
        lineInfos.insert(lineInfos.end(), inLineInfos.begin(), inLineInfos.end());

        // sort the read lines information
        std::sort (lineInfos.begin(), lineInfos.end(), TimedValInfosComparator<InputFileLineInfoType>());

        bool gaps = false;
        int validLines = 0;
        int prevWeekNo = -1;

        typename std::vector<InputFileLineInfoType>::const_iterator infosIter;
        for (infosIter = lineInfos.begin(); infosIter != lineInfos.end(); ++infosIter)
        {
            if (vegetationStartWeek <= infosIter->weekNo) {
                // we found a valid line
                if (prevWeekNo == -1) {
                    prevWeekNo = infosIter->weekNo;
                    continue;
                }
                if (infosIter->weekNo - prevWeekNo > 1) {
                    gaps = true;
                    break;
                }
                prevWeekNo = infosIter->weekNo;
                // we ignore the first valid line
                validLines++;
            }
        }
        if (gaps || validLines == 0) {
            return false;
        }

        return true;
    }

    void KeepCommonDates(const std::vector<InputFileLineInfoType> &lineInfos1, const std::vector<InputFileLineInfoType> &lineInfos2,
                         std::vector<InputFileLineInfoType> &retLineInfos1, std::vector<InputFileLineInfoType> &retLineInfos2) {
        if (lineInfos1.size() == 0 || lineInfos2.size() == 0) {
            return;
        }
        for (size_t i = 0; i<lineInfos1.size(); i++) {
            for (size_t j = 0; j<lineInfos2.size(); j++) {
                if (lineInfos1[i].ttDate == lineInfos2[j].ttDate) {
                    retLineInfos1.push_back(lineInfos1[i]);
                    retLineInfos2.push_back(lineInfos2[j]);
                }
            }
        }
        if (m_bVerbose) {
            otbAppLogINFO("Kept a number of " << retLineInfos1.size() << " common lines!");
        }
    }

    std::vector<InputFileLineInfoType> FilterDuplicates(const std::vector<InputFileLineInfoType> &lineInfos) {
        std::vector<InputFileLineInfoType> retInfos;
        for (std::vector<InputFileLineInfoType>::const_iterator it1 = lineInfos.begin(); it1 != lineInfos.end(); ++it1) {
            bool found = false;
            for (std::vector<InputFileLineInfoType>::const_iterator it2 = retInfos.begin(); it2 != retInfos.end(); ++it2) {
                if (it1->ttDate == it2->ttDate && IsEqual(it1->meanVal, it2->meanVal) && IsEqual(it1->stdDev, it2->stdDev)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                retInfos.push_back(*it1);
            }
        }
        return retInfos;
    }

    bool GroupAndMergeAllData(const FieldInfoType &fieldInfos, const std::vector<InputFileLineInfoType> &ampVHLines,
                              const std::vector<InputFileLineInfoType> &ampVVLines,
                              const std::vector<InputFileLineInfoType> &ndviLines, const std::vector<InputFileLineInfoType> &coheVVLines,
                              std::vector<MergedDateAmplitudeType> &mergedAmpInfos, std::vector<GroupedMeanValInfosType> &ampRatioGroups,
                              std::vector<GroupedMeanValInfosType> &ndviGroups, std::vector<GroupedMeanValInfosType> &coherenceGroups,
                              std::vector<MergedAllValInfosType> &allMergedValues) {
        // Read VV and VH backscatter (grd) and compute backscatter ratio (VV-VH)
        if (m_bVerbose) {
            otbAppLogINFO("Merging amplitudes for field  " << fieldInfos.fieldId);
        }
        if (!MergeAmplitudes(fieldInfos.fieldId, ampVVLines, ampVHLines, mergedAmpInfos)) {
            return false;
        }

        // DEBUG
//        m_debugPrinter.PrintAmplitudeInfos(fieldInfos);
        // DEBUG

        if (m_bVerbose) {
            otbAppLogINFO("Grouping amplitudes by weeks for field  " << fieldInfos.fieldId);
        }
        // Group backscatter ratio by weeks (7-day period) - compute week-mean ($grd.mean)
        if (!GroupTimedValuesByWeeks(mergedAmpInfos, ampRatioGroups)) {
            return false;
        }

        // DEBUG
        //m_debugPrinter.PrintAmpGroupedMeanValues(ampRatioGroups);
        // DEBUG

        if (m_bVerbose) {
            otbAppLogINFO("Computing amplitude 3 weeks mean - current value mean for field  " << fieldInfos.fieldId);
        }
        // Compute 3-weeks-mean before a week ($grd.w3m)
        // and compute difference between week-mean and previous 3-week-mean ($grd.change)
        if (!Compute3WeeksAmplRatioDiffs(ampRatioGroups)) {
            return false;
        }

        // DEBUG
//        m_debugPrinter.PrintAmpGroupedMeanValues(ampRatioGroups);
        // DEBUG

        // DEBUG
//        m_debugPrinter.PrintNdviInfos(fieldInfos);
        // DEBUG

        if (m_bVerbose) {
            otbAppLogINFO("Grouping NDVI by weeks for field  " << fieldInfos.fieldId);
        }
        if (!GroupTimedValuesByWeeks(ndviLines, ndviGroups)) {
            return false;
        }

        // round the values from the NDVI groupes
        std::transform(ndviGroups.begin(), ndviGroups.end(), ndviGroups.begin(),
                       [](GroupedMeanValInfosType &x) {
                x.meanVal = std::round(x.meanVal);
                return x;
        });

        // DEBUG
        m_debugPrinter.PrintNdviGroupedMeanValues(ndviGroups);
        // DEBUG

        // DEBUG
        m_debugPrinter.PrintCoherenceInfos(fieldInfos);
        // DEBUG

        if (m_bVerbose) {
            otbAppLogINFO("Grouping Coherence by weeks for field  " << fieldInfos.fieldId);
        }
        if (!GroupTimedValuesByWeeks(coheVVLines, coherenceGroups)) {
            return false;
        }

        // round the coherence change values
        // round the values from the NDVI groupes
        std::transform(coherenceGroups.begin(), coherenceGroups.end(), coherenceGroups.begin(),
                       [](GroupedMeanValInfosType &x) {
                x.maxChangeVal = (std::round(x.maxChangeVal * 1000) / 1000);
                return x;
        });

        // DEBUG
//        m_debugPrinter.PrintCoherenceGroupedMeanValues(coherenceGroups);
        // DEBUG

        if (m_bVerbose) {
            otbAppLogINFO("Merging all information for field  " << fieldInfos.fieldId);
        }
        if (!MergeAllFieldInfos(fieldInfos, ampRatioGroups, coherenceGroups,
                                ndviGroups, allMergedValues)) {
            return false;
        }
        return true;
    }

    // Compute L_DATE
    time_t GetMaxCohDate(const FieldInfoType &fieldInfos, const std::vector<MergedAllValInfosType> &allMergedValues) {
        time_t ttMaxCohDate = 0;
        time_t ttLastMergedValuesDate = allMergedValues[allMergedValues.size()-1].ttDate;
        time_t ttCurFloorDate;
        for (const InputFileLineInfoType &linfo : fieldInfos.coheVVLines) {
            ttCurFloorDate = linfo.GetFloorTime();
            if (ttCurFloorDate == ttLastMergedValuesDate && ttCurFloorDate > ttMaxCohDate) {
                ttMaxCohDate = ttCurFloorDate;
            }
        }
        return ttMaxCohDate;
    }

    bool ProcessFieldInformation(FieldInfoType &fieldInfos) {

        std::vector<MergedAllValInfosType> allMergedValues;
        if (!GroupAndMergeAllData(fieldInfos, fieldInfos.ampVHLines, fieldInfos.ampVVLines, fieldInfos.ndviLines, fieldInfos.coheVVLines,
                                  fieldInfos.mergedAmpInfos, fieldInfos.ampRatioGroups, fieldInfos.ndviGroups, fieldInfos.coherenceGroups,
                                  allMergedValues)) {
            return false;
        }

        // ### TIME SERIES ANALYSIS FOR HARVEST ###
        CheckVegetationStart(fieldInfos, allMergedValues);

        UpdateMarker1Infos(fieldInfos, allMergedValues);
        UpdateMarker2Infos(fieldInfos, allMergedValues);
        UpdateMarker5Infos(fieldInfos, allMergedValues);
        double ampThrValue;
        UpdateMarker3Infos(fieldInfos, allMergedValues, ampThrValue);
        UpdateMarker4Infos(fieldInfos, allMergedValues, ampThrValue);

        // DEBUG
        m_debugPrinter.PrintMergedValues(allMergedValues, ampThrValue);
        // DEBUG

        HarvestEvaluationInfoType harvestInfos;
        HarvestEvaluation(fieldInfos, allMergedValues, harvestInfos);

        // update the gaps information
        UpdateGapsInformation(allMergedValues, fieldInfos, harvestInfos);

//      DEBUG
        m_debugPrinter.PrintHarvestEvaluation(fieldInfos, harvestInfos);
//      DEBUG

        HarvestEvaluationInfoType efaHarvestEvalInfos;
        bool hasEfaInfos = false;
        // ### TIME SERIES ANALYSIS FOR EFA PRACTICES ###
        if (fieldInfos.practiceName != NA_STR && fieldInfos.ttPracticeStartTime != 0) {
            if (fieldInfos.practiceName.find(m_CatchCropVal) != std::string::npos) {
                CatchCropPracticeAnalysis(fieldInfos, allMergedValues, harvestInfos, efaHarvestEvalInfos);
            } else if (fieldInfos.practiceName == m_FallowLandVal) {
                FallowLandPracticeAnalysis(fieldInfos, allMergedValues, harvestInfos, efaHarvestEvalInfos);
            } else if (fieldInfos.practiceName == m_NitrogenFixingCropVal) {
                NitrogenFixingCropPracticeAnalysis(fieldInfos, allMergedValues, harvestInfos, efaHarvestEvalInfos);
            } else {
                otbAppLogWARNING("Practice name " << fieldInfos.practiceName << " not supported!");
            }
            time_t ttMaxCohDate = GetMaxCohDate(fieldInfos, allMergedValues);
            if (efaHarvestEvalInfos.ttPracticeEndTime > ttMaxCohDate) {
                efaHarvestEvalInfos.efaIndex = NR_STR;
            }
            hasEfaInfos = true;
        }

        // write infos to be generated as plots
        m_plotsWriter.WritePlotEntry(fieldInfos, harvestInfos, efaHarvestEvalInfos, hasEfaInfos);

        // Write the continuous field infos into the file
        m_contFileWriter.WriteContinousToCsv(fieldInfos, allMergedValues);

        // write the harvest information to the final file
        m_csvWriter.WriteHarvestInfoToCsv(fieldInfos, harvestInfos, efaHarvestEvalInfos);

        return true;
    }

    bool MergeAmplitudes(const std::string &fieldId, const std::vector<InputFileLineInfoType> &ampVVLines,
                         const std::vector<InputFileLineInfoType> &ampVHLines,
                         std::vector<MergedDateAmplitudeType> &retInfos) {
        if (ampVVLines.size() == 0) {
            otbAppLogWARNING("The amplitude file info sizes is zero for field " << fieldId);
            return false;
        }
        if (ampVVLines.size() != ampVHLines.size()) {
            otbAppLogWARNING("The extracted amplitude file sizes differ for field " << fieldId);
            return false;
        }
        retInfos.resize(ampVVLines.size());
        // Theoretically we should have at the same position the same date
        for (size_t i = 0; i<ampVVLines.size(); i++) {
            const InputFileLineInfoType &vvLine = ampVVLines[i];
            const InputFileLineInfoType &vhLine = ampVHLines[i];
            if (vvLine.ttDate != vhLine.ttDate) {
                otbAppLogWARNING("Date inconsistency detected for amplitudes at index " << i << " for field " << fieldId);
                return false;
            }
            retInfos[i].vvInfo = vvLine;
            retInfos[i].vhInfo = vhLine;
            retInfos[i].ampRatio = vvLine.meanVal - vhLine.meanVal;
            retInfos[i].ttDate = vvLine.ttDate;
        }
        return true;
    }

    template <typename TimedValue>
    bool GroupTimedValuesByWeeks(const std::vector<TimedValue> &infos,
                                std::vector<GroupedMeanValInfosType> &retGroups) {
        double curMaxVal;
        double curMaxChangeVal;
        double curVal;
        double curChangeVal;
        time_t ttTime;
        std::map<time_t, GroupedMeanValInfosType> mapGroups;
        for (const auto &info: infos) {
            ttTime = info.GetFloorTime();
            curMaxVal = mapGroups[ttTime].maxVal;
            curVal = info.GetValue();
            mapGroups[ttTime].ttDate = ttTime;
            mapGroups[ttTime].maxVal = IsLess(curMaxVal, curVal) ? curVal : curMaxVal ;
            mapGroups[ttTime].sum += curVal;
            mapGroups[ttTime].cnt++;
            mapGroups[ttTime].meanVal = (mapGroups[ttTime].sum / mapGroups[ttTime].cnt);

            // Get the change value, if supported
            if (info.GetChangeValue(curChangeVal)) {
                curMaxChangeVal = mapGroups[ttTime].maxChangeVal;
                mapGroups[ttTime].maxChangeVal = IsLess(curMaxChangeVal, curChangeVal) ? curChangeVal : curMaxChangeVal ;
            }
        }

        for( std::map<time_t, GroupedMeanValInfosType>::iterator it = mapGroups.begin(); it != mapGroups.end(); ++it ) {
                retGroups.push_back( it->second );
        }
        std::sort(retGroups.begin(), retGroups.end(), TimedValInfosComparator<GroupedMeanValInfosType>());

        return true;
    }

    // Compute 3-weeks-mean before a week and then
    // Compute difference between week-mean and previous 3-week-mean
    bool Compute3WeeksAmplRatioDiffs(std::vector<GroupedMeanValInfosType> &ampRatioGroups) {
        double cur3WeeksMeanVal;
        for (size_t i = 0; i<ampRatioGroups.size(); i++)
        {
            if (i >= 3) {
                // Compute 3-weeks-mean before a week ($grd.w3m)
                cur3WeeksMeanVal = 0;
                for (int j = 1; j<=3; j++) {
                    cur3WeeksMeanVal += ampRatioGroups[i - j].meanVal;
                }
                cur3WeeksMeanVal /= 3;
                // Compute difference between week-mean and previous 3-week-mean
                ampRatioGroups[i].ampChange = ampRatioGroups[i].meanVal - cur3WeeksMeanVal;
                if (ampRatioGroups[i].ampChange < 0) {
                    ampRatioGroups[i].ampChange = 0;
                } else {
                    // round the value to 2 decimals
                    ampRatioGroups[i].ampChange = std::round(ampRatioGroups[i].ampChange * 100)/100;
                }
            }
        }

        return true;
    }

    bool MergeAllFieldInfos(const FieldInfoType &fieldInfos, const std::vector<GroupedMeanValInfosType> &ampRatioGroups,
                            const std::vector<GroupedMeanValInfosType> &coherenceGroups,
                            const std::vector<GroupedMeanValInfosType> &ndviGroups,
                            std::vector<MergedAllValInfosType> &retAllMergedValues)
    {
        if (!MergeAmpCoheFieldInfos(fieldInfos, ampRatioGroups, coherenceGroups, retAllMergedValues)) {
            return false;
        }

        // update the ret list size with the right value
        size_t retListSize = retAllMergedValues.size();
        if (m_bVerbose) {
            otbAppLogINFO("Kept a common AMP-COHE values of " << retListSize);
        }
        if (retListSize == 0) {
            otbAppLogWARNING("No common AMP-COHE values detected for the parcel with ID " << fieldInfos.fieldId);
            return false;
        }

        // Fill the NDVI fields
        // In this case, we might have gaps


//      RAW IMPLEMENTATION - NOT VERIFIED
/*        for(size_t i = 0; i<retListSize; i++) {
            for(size_t j = 0; j<ndviGroups.size(); j++) {
                if(retAllMergedValues[i].ttDate == ndviGroups[j].ttDate) {
                    retAllMergedValues[i].ndviMeanVal = ndviGroups[j].meanVal;
                    break;
                }
            }
        }
        for(size_t i = 0; i<retListSize; i++) {
            if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime) {
                retAllMergedValues[i].ndviPrev = retAllMergedValues[i].ndviMeanVal;
                retAllMergedValues[i].ndviNext = retAllMergedValues[i].ndviMeanVal;
            } else {
                retAllMergedValues[i].ndviPrev = NOT_AVAILABLE;
                retAllMergedValues[i].ndviNext = NOT_AVAILABLE;
            }
        }
        for(size_t i = 0; i<retListSize-1; i++) {
            if (!IsNA(retAllMergedValues[i].ndviPrev) && IsNA(retAllMergedValues[i+1].ndviPrev)) {
                retAllMergedValues[i+1].ndviPrev = retAllMergedValues[i].ndviPrev;
            }
        }
        for (int i = (int)retListSize-1; i>=1; i--) {
            if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime &&
                    !IsNA(retAllMergedValues[i].ndviNext) && IsNA(retAllMergedValues[i-1].ndviNext)) {
                retAllMergedValues[i-1].ndviNext = retAllMergedValues[i].ndviNext;
            }
        }

        for(size_t i = 1; i<retListSize; i++) {
            if (retAllMergedValues[i].ndviNext == retAllMergedValues[i].ndviPrev) {
                retAllMergedValues[i].ndviPrev = retAllMergedValues[i-1].ndviPrev;
            }
        }
        for(size_t i = 0; i<retListSize; i++) {
            if (!IsNA(retAllMergedValues[i].ndviMeanVal) && IsNA(retAllMergedValues[i].ndviPrev) &&
                !IsNA(retAllMergedValues[i].ndviNext)) {
                retAllMergedValues[i].ndviPrev = retAllMergedValues[i].ndviNext;
            }
        }

*/

        // ORIGINAL OPTMIZED IMPLEMENTATION
        double prevNdviVal = NOT_AVAILABLE;
        for(size_t i = 0; i<retListSize; i++) {
            bool valueSet = false;
            for(size_t j = 0; j<ndviGroups.size(); j++) {
                if(retAllMergedValues[i].ttDate == ndviGroups[j].ttDate) {
                    retAllMergedValues[i].ndviMeanVal = ndviGroups[j].meanVal;
                    valueSet = true;
                    break;
                }
            }
            // fill the NDVI prev values
            if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime) {
                if (valueSet) {
                    if (IsNA(prevNdviVal)) {
                        // set the current value if NA previously
                        retAllMergedValues[i].ndviPrev = retAllMergedValues[i].ndviMeanVal;
                    } else {
                        // in this case, if exists a prevVal, set it
                        retAllMergedValues[i].ndviPrev = prevNdviVal;
                    }
                    // save current value the previously var
                    prevNdviVal = retAllMergedValues[i].ndviMeanVal;
                } else {
                    // just keep the previous value, whatever it is
                    retAllMergedValues[i].ndviPrev = prevNdviVal;
                }
            }
        }

        // fill the NDVI next values
        double nextNdviVal = NOT_AVAILABLE;
        // iterate backwards for filling the next values
        for (int i = (int)retListSize-1; i>=1; i--) {
            if (retAllMergedValues[i].ttDate < fieldInfos.ttVegStartWeekFloorTime) {
                // Normally, there is no need to continue
                break;
            }
            if (!IsNA(retAllMergedValues[i].ndviMeanVal)) {
                nextNdviVal = retAllMergedValues[i].ndviMeanVal;
                retAllMergedValues[i].ndviNext = nextNdviVal;
            }
            retAllMergedValues[i-1].ndviNext = nextNdviVal;
        }

        for(size_t i = 0; i<retListSize; i++) {
            if (!IsNA(retAllMergedValues[i].ndviMeanVal) && IsNA(retAllMergedValues[i].ndviPrev) && !IsNA(retAllMergedValues[i].ndviNext)) {
                retAllMergedValues[i].ndviPrev = retAllMergedValues[i].ndviNext;
            }
        }

        // print the values
//        for(size_t i = 0; i<retListSize; i++) {
//            std::cout << retAllMergedValues[i].ndviMeanVal << ";" << retAllMergedValues[i].ndviPrev <<
//                         ";" << retAllMergedValues[i].ndviNext << ";" << std::endl;
//        }

        return true;
    }

    bool MergeAmpCoheFieldInfos(const FieldInfoType &fieldInfos, const std::vector<GroupedMeanValInfosType> &ampRatioGroups,
                            const std::vector<GroupedMeanValInfosType> &coherenceGroups,
                            std::vector<MergedAllValInfosType> &retAllMergedValues)
    {
        if (ampRatioGroups.size() != coherenceGroups.size() && m_bVerbose) {
            otbAppLogWARNING("Amplitude and coherence groups sizes differ when merging for field " << fieldInfos.fieldId <<
                             " Amp size: " << ampRatioGroups.size() << " Cohe size: " << coherenceGroups.size());
        }
/*
        // get the minimum and the maximum dates from the amp and cohe
        time_t ttMinDate = (ampRatioGroups[0].ttDate < coherenceGroups[0].ttDate) ?
                    ampRatioGroups[0].ttDate : coherenceGroups[0].ttDate;
        time_t ttMaxDate = (ampRatioGroups[ampRatioGroups.size()-1].ttDate > coherenceGroups[coherenceGroups.size()-1].ttDate) ?
                    ampRatioGroups[ampRatioGroups.size()-1].ttDate : coherenceGroups[coherenceGroups.size()-1].ttDate;
        time_t curDate = ttMinDate;
        while(curDate <= ttMaxDate) {
            int ampIdx = -1;
            for(size_t i = 0; i < ampRatioGroups.size(); i++) {
                if (ampRatioGroups[i].ttDate == curDate) {
                    ampIdx = i;
                    break;
                }
            }
            int coheIdx = -1;
            for(size_t i = 0; i < coherenceGroups.size(); i++) {
                if (coherenceGroups[i].ttDate == curDate) {
                    coheIdx = i;
                    break;
                }
            }
            MergedAllValInfosType mergedVal;

            mergedVal.ttDate = curDate;
            mergedVal.ampMean = (ampIdx == -1) ? NOT_AVAILABLE : ampRatioGroups[ampIdx].meanVal;
            mergedVal.ampMax = (ampIdx == -1) ? NOT_AVAILABLE : ampRatioGroups[ampIdx].maxVal;
            mergedVal.ampChange = (ampIdx == -1) ? NOT_AVAILABLE : ampRatioGroups[ampIdx].ampChange;

            mergedVal.cohChange = (coheIdx == -1) ? 0 : coherenceGroups[coheIdx].maxChangeVal;
            mergedVal.cohMax = (coheIdx == -1) ? 0 : coherenceGroups[coheIdx].maxVal;

            retAllMergedValues.push_back(mergedVal);

            curDate += SEC_IN_WEEK;
        }
*/
// ORIGINAL IMPLEMENTATION WORKING WITHOUT GAPS

        bool refListIsAmp = ampRatioGroups.size() <= coherenceGroups.size();
        size_t retListSize = refListIsAmp ? ampRatioGroups.size() : coherenceGroups.size();
        const std::vector<GroupedMeanValInfosType> &refList1 = refListIsAmp ? ampRatioGroups : coherenceGroups;
        const std::vector<GroupedMeanValInfosType> &refList2 = refListIsAmp ? coherenceGroups : ampRatioGroups;

        // we keep the dates where we have both backscatter and coherence
        for(size_t i = 0; i < retListSize; i++) {
            const GroupedMeanValInfosType &refItem1 = refList1[i];
            for (size_t j = 0; j<refList2.size(); j++) {
                const GroupedMeanValInfosType &refItem2 = refList2[j];
                if (refItem1.ttDate == refList2[j].ttDate) {
                    MergedAllValInfosType mergedVal;
                    const GroupedMeanValInfosType &ampItem = refListIsAmp ? refItem1 : refItem2;
                    const GroupedMeanValInfosType &coheItem = refListIsAmp ? refItem2 : refItem1;
                    mergedVal.ttDate = ampItem.ttDate;
                    mergedVal.ampMean = ampItem.meanVal;
                    mergedVal.ampMax = ampItem.maxVal;
                    mergedVal.ampChange = ampItem.ampChange;

                    mergedVal.cohChange = coheItem.maxChangeVal;
                    mergedVal.cohMax = coheItem.maxVal;
                    retAllMergedValues.push_back(mergedVal);
                    break;
                }
            }
        }

        return true;
    }

    bool GroupAndMergeFilteredData(const FieldInfoType &fieldInfos, time_t ttStartTime, time_t ttEndTime,
                                   std::vector<MergedDateAmplitudeType> &mergedAmpInfos,
                                   std::vector<MergedAllValInfosType> &allMergedValues) {
        const std::vector<InputFileLineInfoType> &ndviFilteredValues = FilterValuesByDates(
                    fieldInfos.ndviLines, ttStartTime, ttEndTime);
        const std::vector<InputFileLineInfoType> &coheVVFilteredValues = FilterValuesByDates(
                    fieldInfos.coheVVLines, ttStartTime, ttEndTime);

        // TODO: Ask Gisat why amplitude is taken for an interval > 1 week?
        const std::vector<InputFileLineInfoType> &ampVVFilteredValues = FilterValuesByDates(
                    fieldInfos.ampVVLines, ttStartTime, ttEndTime + SEC_IN_WEEK);
        const std::vector<InputFileLineInfoType> &ampVHFilteredValues = FilterValuesByDates(
                    fieldInfos.ampVHLines, ttStartTime, ttEndTime + SEC_IN_WEEK);

        std::vector<GroupedMeanValInfosType> ampRatioGroups;
        std::vector<GroupedMeanValInfosType> ndviGroups;
        std::vector<GroupedMeanValInfosType> coherenceGroups;

        if (!GroupAndMergeAllData(fieldInfos, ampVHFilteredValues, ampVVFilteredValues, ndviFilteredValues, coheVVFilteredValues,
                                  mergedAmpInfos, ampRatioGroups, ndviGroups, coherenceGroups,
                                  allMergedValues)) {
            return false;
        }
        return true;
    }

    template <typename Value>
    std::vector<Value> FilterValuesByDates(const std::vector<Value> &mergedValues,
                                                      time_t ttStartTime, time_t ttEndTime) const {
        // if no filter, return the initial vector
        int vecSize = mergedValues.size();
        if ((ttStartTime == 0 && ttEndTime == 0) || (vecSize == 0)) {
            return mergedValues;
        }
        // assuming they are sorted, check the first and the last
        if (ttStartTime <= mergedValues[0].ttDate &&
                ttEndTime >= mergedValues[vecSize-1].ttDate) {
            return mergedValues;
        }
        // we need to filter values
        std::vector<Value> retVect;
        for (typename std::vector<Value>::const_iterator it = mergedValues.begin(); it != mergedValues.end(); ++it) {
            if(it->ttDate >= ttStartTime && it->ttDate <= ttEndTime) {
                retVect.push_back(*it);
            }
        }
        return retVect;
    }

    void CheckVegetationStart(FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues) {
        // # to avoid gap in vegseason.start week
        // if (length(which(group.merge$Group.1==vegseason.start)==vegseason.start)==0) {
        //    vegseason.start <- min(group.merge[which(group.merge$Group.1>=vegseason.start),]$Group.1);
        //    base.info$VEG_START <- vegseason.start;
        // }
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (retAllMergedValues[i].ttDate == fieldInfos.ttVegStartWeekFloorTime) {
                break;
            } else if (retAllMergedValues[i].ttDate > fieldInfos.ttVegStartWeekFloorTime) {
                // Get the first date that is greater than ttVegStartWeekFloorTime if we did not had equality
                // we assume that the dates are already sorted in cronological order
                fieldInfos.ttVegStartWeekFloorTime = retAllMergedValues[i].ttDate;
                fieldInfos.ttVegStartTime = retAllMergedValues[i].ttDate;
                break;
            }
        }
    }
    void UpdateMarker1Infos(FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues) {
        // # to avoid gap in vegseason.start week
        bool bVegStartFound = false;
        int minVegStartDateIdx = -1;
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (retAllMergedValues[i].ttDate == fieldInfos.ttVegStartWeekFloorTime) {
                bVegStartFound = true;
                break;
            } else if (retAllMergedValues[i].ttDate > fieldInfos.ttVegStartWeekFloorTime) {
                // get the first date greater than veg start
                minVegStartDateIdx = i;
                break;
            }
        }
        if (!bVegStartFound && minVegStartDateIdx > 0) {
            fieldInfos.ttVegStartTime = retAllMergedValues[minVegStartDateIdx].ttDate;
            fieldInfos.ttVegStartWeekFloorTime = retAllMergedValues[minVegStartDateIdx].ttDate;
        }

        // MARKER 1  Presence of vegetation cycle (NDVI): M1 == $ndvi.presence
        // Define weeks in vegetation season
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime &&
                retAllMergedValues[i].ttDate <= fieldInfos.ttHarvestEndWeekFloorTime) {
                retAllMergedValues[i].vegWeeks = true;
            }
        }
        if (fieldInfos.practiceName == "CatchCrop" && fieldInfos.ttPracticeStartTime != 0) {
            // # if catch crop is second crop -> shorten the veg.weeks period
            if (fieldInfos.practiceType != m_CatchMain && fieldInfos.practiceType != m_CatchCropIsMain &&
                    fieldInfos.ttVegStartWeekFloorTime < fieldInfos.ttPracticeStartWeekFloorTime) {
                time_t ttPractStartNextWeek = fieldInfos.ttPracticeStartWeekFloorTime + SEC_IN_WEEK;
                for(size_t i = 0; i<retAllMergedValues.size(); i++) {
                    if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime &&
                        retAllMergedValues[i].ttDate <= ttPractStartNextWeek) {
                        retAllMergedValues[i].vegWeeks = true;
                    }
                }
            }
        }

        // if( length(which(group.merge$Group.1==harvest.to ))==0  ){harvest.to <- group.merge[nrow(group.merge),]$Group.1}
        bool bHarvestEndFound = false;
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (retAllMergedValues[i].ttDate == fieldInfos.ttHarvestEndWeekFloorTime) {
                bHarvestEndFound = true;
                break;
            }
        }
        if (!bHarvestEndFound) {
            fieldInfos.ttHarvestEndWeekFloorTime = retAllMergedValues[retAllMergedValues.size()-1].ttDate;
        }

        // Define presence of NDVI (M1) for vegetation season
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime &&
                retAllMergedValues[i].ttDate <= fieldInfos.ttHarvestEndWeekFloorTime) {
                if (IsNA(retAllMergedValues[i].ndviMeanVal)) {
                    retAllMergedValues[i].ndviPresence = false;
                } else if (IsGreater(retAllMergedValues[i].ndviMeanVal, m_OpticalThrVegCycle)) {
                    // here we must set all the values from i to the date of Harvest End to true
                    for (size_t j = i; j < retAllMergedValues.size() && retAllMergedValues[j].ttDate <= fieldInfos.ttHarvestEndWeekFloorTime; j++) {
                        retAllMergedValues[j].ndviPresence = true;
                    }
                    break;  // exit now, all needed entries were processed
                } else {
                    retAllMergedValues[i].ndviPresence = false;
                }
            }
        }
    }

    void UpdateMarker2Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues) {
        double prevValidNdviMeanVal = NOT_AVAILABLE;

        bool allVwstNdviDw = true; // all vegetation weeks smaller than NDVI down
        bool ndviDropMeanLessDw = false; // at least one NDVI mean value corresponding to a drop is less than NDVI down
        bool ndviDropMeanLessUp = false; // at least one NDVI mean value corresponding to a drop is less than NDVI up
        double minMeanNdviDropVal = NOT_AVAILABLE;
        double maxMeanNdviDropVal = NOT_AVAILABLE;
        int nVegWeeksCnt = 0;
        bool bHasNdviDrop = false;

        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].ndviDrop = NOT_AVAILABLE;
            if (IsNA(retAllMergedValues[i].ndviMeanVal)) {
                continue;
            }
            if (retAllMergedValues[i].vegWeeks == true) {
                if (retAllMergedValues[i].ndviPresence == true) {
                    nVegWeeksCnt++;
                }

                if ((IsNA(retAllMergedValues[i].ndviPresence) || retAllMergedValues[i].ndviPresence == true) && !IsNA(prevValidNdviMeanVal)) {
                    // update the NDVI drop only to the ones that have NDVI presence true
                    retAllMergedValues[i].ndviDrop = (IsLess(retAllMergedValues[i].ndviMeanVal, prevValidNdviMeanVal) &&
                            IsLess(retAllMergedValues[i].ndviMeanVal, m_NdviUp));

                    // compute additional infos for computing opt.thr.value
                    if (retAllMergedValues[i].ndviDrop) {
                        bHasNdviDrop = true;
                        if (IsLess(retAllMergedValues[i].ndviMeanVal, m_NdviDown)) {
                            ndviDropMeanLessDw = true;
                        }
                        if (IsLess(retAllMergedValues[i].ndviMeanVal, m_NdviUp)) {
                            ndviDropMeanLessUp = true;
                        }
                        if (IsNA(minMeanNdviDropVal)) {
                            minMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                        } else {
                            if (IsGreater(minMeanNdviDropVal, retAllMergedValues[i].ndviMeanVal)) {
                                minMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                            }
                        }
                    }
                }
                // save the previous valid ndvi mean value that has veg.weeks = true needed for ndvi.drop calculation
                prevValidNdviMeanVal = retAllMergedValues[i].ndviMeanVal;

                // compute the maximum ndvi drop needed for opt.thr.value computation
                if (IsGreaterOrEqual(retAllMergedValues[i].ndviMeanVal, m_NdviDown)) {
                    allVwstNdviDw = false;
                }
                if (IsNA(maxMeanNdviDropVal)) {
                    maxMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                } else {
                    if (IsLess(maxMeanNdviDropVal, retAllMergedValues[i].ndviMeanVal)) {
                        maxMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                    }
                }
            }
//          DEBUG
//            std::cout<< i+1 << " " << ValueToString(retAllMergedValues[i].ndviDrop, true) << std::endl;
//          DEBUG
        }

        // Compute the opt.thr.value
        double OpticalThresholdValue;
        if (allVwstNdviDw || ndviDropMeanLessDw) {
            OpticalThresholdValue = m_NdviDown;
        } else if (ndviDropMeanLessUp) {
            OpticalThresholdValue = m_ndviStep * std::ceil(minMeanNdviDropVal / m_ndviStep);
        } else {
            OpticalThresholdValue = m_NdviUp;
        }

        double OpticalThresholdBuffer = ComputeOpticalThresholdBuffer(nVegWeeksCnt, maxMeanNdviDropVal, OpticalThresholdValue);

        // Define start of harvest period based on optical data (harvest.opt.start)
        time_t harvestOpticalStart = 0;
        double optThrValBuf = OpticalThresholdValue + OpticalThresholdBuffer;
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            if ((!IsNA(retAllMergedValues[i].ndviNext)) &&
                    IsLess(retAllMergedValues[i].ndviNext, optThrValBuf) &&
                    IsGreater(retAllMergedValues[i].ndviPrev, OpticalThresholdValue) &&
                    IsGreater(retAllMergedValues[i].ndviPrev, retAllMergedValues[i].ndviNext) &&
                    (retAllMergedValues[i].ttDate >= fieldInfos.ttHarvestStartWeekFloorTime)) {
                harvestOpticalStart = retAllMergedValues[i].ttDate;
                break;
            }
        }
        if (harvestOpticalStart == 0) {
            harvestOpticalStart = fieldInfos.ttHarvestStartWeekFloorTime;
        }
//          DEBUG
//        std::cout << TimeToString(fieldInfos.ttHarvestStartWeekFloorTime) << std::endl;
//        std::cout << TimeToString(harvestOpticalStart) << std::endl;
//          DEBUG

        // Define candidate-weeks for harvest based on optical data (M2)
        if (bHasNdviDrop) {
            for(size_t i = 0; i<retAllMergedValues.size(); i++) {
                if (!IsNA(retAllMergedValues[i].ndviNext) && !IsNA(retAllMergedValues[i].ndviPrev)) {
                    if ((IsLess(retAllMergedValues[i].ndviNext, optThrValBuf) ||
                         IsLess(retAllMergedValues[i].ndviPrev, optThrValBuf)) &&
                            IsGreater(retAllMergedValues[i].ndviPrev, m_OpticalThresholdMinimum) &&
                            (retAllMergedValues[i].ttDate >= harvestOpticalStart) &&
                            IsGreater((retAllMergedValues[i].ndviPrev + m_NdviDown), retAllMergedValues[i].ndviNext)) {
                        retAllMergedValues[i].candidateOptical = true;
                    }
                }
            }
        }
    }


    void UpdateMarker3Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                        double &ampThrValue) {
        std::vector<int> ampOutlier;
        std::map<time_t, int> mapCnts;
        std::map<time_t, double> mapSums;
        ampOutlier.resize(fieldInfos.ampVVLines.size());
        for(size_t i = 0; i < fieldInfos.ampVVLines.size(); i++) {
            // for backscatter outliars VV < -3 dB  (draft soulution)
            ampOutlier[i] = (fieldInfos.ampVVLines[i].meanVal > -3) ? 1 : 0;
        }
        // first extract the sum and counts for each date
        for(size_t i = 0; i < fieldInfos.ampVVLines.size(); i++) {
            if (ampOutlier[i] == 0) {
                mapCnts[fieldInfos.ampVVLines[i].ttDateFloor]++;
                mapSums[fieldInfos.ampVVLines[i].ttDateFloor] += fieldInfos.mergedAmpInfos[i].ampRatio;
            }
        }
        // Compute the mean for the filtered amplitudes
        std::vector<double> meanAmpValsFiltered;
        for( std::map<time_t, double>::iterator it = mapSums.begin(); it != mapSums.end(); ++it ) {
            int cntVal = mapCnts[it->first];
            if (cntVal > 0) {
                meanAmpValsFiltered.push_back(it->second / cntVal);
            }
        }
//          DEBUG
//        for (int i = 0; i<meanAmpValsFiltered.size(); i++) {
//            std::cout << meanAmpValsFiltered[i] << std::endl;
//        }
//          DEBUG
        // Compute the standard deviation
        double meanVal = 0;
        double stdDevVal = 0;
        // Compute Sample Standard Deviation instead of Population Standard Deviation
        // TODO: To be confirmed by Gisat
        ComputeMeanAndStandardDeviation(meanAmpValsFiltered, meanVal, stdDevVal, true);

//        grd.thr.break <- paste("grd.thr.break <- sd(grd.filtered)/6")
//        grd.thr.value <- paste("grd.thr.value <- mean(grd.filtered)-sd(grd.filtered)/2")
        double ampThrBreak = ComputeAmplitudeThresholdBreak(stdDevVal);
        ampThrValue = ComputeAmplitudeThresholdValue(meanVal, stdDevVal);
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].candidateAmplitude = IsGreater(retAllMergedValues[i].ampChange, ampThrBreak);
//          DEBUG
//            std::cout << ValueToString(retAllMergedValues[i].candidateAmplitude, true) << std::endl;
//          DEBUG
        }

        // we iterate from index 1 to index size - 2 but we access also these indexes
        std::vector<int> indexesToUpdate;
        for(size_t i = 1; i<retAllMergedValues.size()-1; i++) {
            if (IsGreater(retAllMergedValues[i].cohChange, m_CohThrHigh) &&
                    ((retAllMergedValues[i-1].candidateAmplitude) ||
                     (retAllMergedValues[i].candidateAmplitude) ||
                     (retAllMergedValues[i+1].candidateAmplitude))) {
                indexesToUpdate.push_back(i);
                // Note: we do not set here as we change the value for the next item that might
                // be incorrectly set to true. This is why we save the indexes and make the update later
                //retAllMergedValues[i].candidateAmplitude = true;
            }
        }
        for (auto idx: indexesToUpdate) {
            retAllMergedValues[idx].candidateAmplitude = true;
        }
    }


    void UpdateMarker4Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                            double &ampThrValue) {
        (void)fieldInfos;    // supress unused warning
        // MARKER 4  Presence of vegetation cycle (BACKSCATTER): M4 == $grd.presence
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].amplitudePresence = IsGreater(retAllMergedValues[i].ampMax, ampThrValue);
            if (IsGreater(retAllMergedValues[i].cohChange, m_CohThrHigh) &&
                    (retAllMergedValues[i].ndviPresence == true) &&
                    (retAllMergedValues[i].candidateOptical == true) &&
                    (retAllMergedValues[i].candidateAmplitude == true) &&
                    !retAllMergedValues[i].amplitudePresence ) {
                retAllMergedValues[i].amplitudePresence = TRUE;
            }
        }
    }


    void UpdateMarker5Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues) {
         // Define candidate-weeks for harvest based on coherence data (M5)
        double CohThrAbs = m_CohThrAbs;
        if (IsLess(fieldInfos.coheVVMaxValue, m_CohThrAbs)) {
            CohThrAbs = fieldInfos.coheVVMaxValue - m_CohThrBase;
        }

        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].coherenceBase = IsGreaterOrEqual(retAllMergedValues[i].cohChange, m_CohThrBase);
            retAllMergedValues[i].coherenceHigh = IsGreaterOrEqual(retAllMergedValues[i].cohChange, m_CohThrHigh);
            retAllMergedValues[i].coherencePresence = IsGreaterOrEqual(retAllMergedValues[i].cohMax, CohThrAbs);
            retAllMergedValues[i].candidateCoherence =
                    (retAllMergedValues[i].coherenceBase || retAllMergedValues[i].coherencePresence);
        }
    }

    bool HarvestEvaluation(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                           HarvestEvaluationInfoType &harvestEvalInfos) {
        // TODO: This can be done using a flag in a structure encapsulating also retAllMergedValues
        //      in order to avoid this iteration (can be done in an iteration previously made)
        harvestEvalInfos.Initialize(fieldInfos);
        // Update the L_Week time
        harvestEvalInfos.ttLWeekStartTime = retAllMergedValues[retAllMergedValues.size() - 1].ttDate;

        unsigned int nSeqIdNo = std::atoi(fieldInfos.fieldSeqId.c_str());
        const std::string &prevHWeekStartStr = m_prevPrdReader.GetHWeekStartForFieldId(nSeqIdNo);
        time_t ttprevHWeekStart = 0;
        if (prevHWeekStartStr.size() > 0) {
            ttprevHWeekStart = GetTimeFromString(prevHWeekStartStr);
        }

        if (!HasValidNdviValues(retAllMergedValues, m_OpticalThrVegCycle, true)) {
            harvestEvalInfos.ndviPresence = false;               // M1
            harvestEvalInfos.candidateOptical = NR;           // M2
            harvestEvalInfos.candidateAmplitude = NR;         // M3
            harvestEvalInfos.amplitudePresence = NR;          // M4
            harvestEvalInfos.candidateCoherence = NR;         // M5
            harvestEvalInfos.harvestConfirmWeek = (ttprevHWeekStart != 0 ? GetWeekFromDate(ttprevHWeekStart) : NR);
            harvestEvalInfos.ttHarvestConfirmWeekStart = (ttprevHWeekStart != 0 ? ttprevHWeekStart : NR);
            harvestEvalInfos.ttS1HarvestWeekStart = NR;
            return false;
        }

        int idxFirstHarvest = -1;
        int idxFirstS1Harvest = -1;
        bool harvestS1;
        time_t harvestConfirmWeekStart = NOT_AVAILABLE;
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            harvestS1 = ((retAllMergedValues[i].ndviPresence == true) && (retAllMergedValues[i].candidateAmplitude == true) &&
                         (retAllMergedValues[i].amplitudePresence == true) && (retAllMergedValues[i].candidateCoherence == true) &&
                         retAllMergedValues[i].ttDate >= fieldInfos.ttHarvestStartWeekFloorTime);

            if (idxFirstS1Harvest == -1 && harvestS1) {
                idxFirstS1Harvest = i;
            }

            retAllMergedValues[i].harvest = (harvestS1 && (retAllMergedValues[i].candidateOptical == true));

            // save the first == week of harvest
            if (retAllMergedValues[i].harvest && idxFirstHarvest == -1) {
                idxFirstHarvest = i;
                harvestConfirmWeekStart = retAllMergedValues[i].ttDate;
            }
        }

        // update the harvestConfirmed with the value from the previous file, if present and different
        if (ttprevHWeekStart != 0) {
            harvestConfirmWeekStart = ttprevHWeekStart;
        }

        int lastAvailableIdx = retAllMergedValues.size() - 1;
        harvestEvalInfos.ndviPresence = retAllMergedValues[lastAvailableIdx].ndviPresence;               // M1
        harvestEvalInfos.candidateOptical = retAllMergedValues[lastAvailableIdx].candidateOptical;       // M2
        harvestEvalInfos.candidateAmplitude = retAllMergedValues[lastAvailableIdx].candidateAmplitude;   // M3
        harvestEvalInfos.amplitudePresence = retAllMergedValues[lastAvailableIdx].amplitudePresence;     // M4
        harvestEvalInfos.candidateCoherence = retAllMergedValues[lastAvailableIdx].candidateCoherence;   // M5

        // "HARVESTED CONDITIONS NOT DETECTED"
        if (IsNA(harvestConfirmWeekStart)) {
            // report results from last available week
            harvestEvalInfos.harvestConfirmWeek = NR;
            harvestEvalInfos.ttHarvestConfirmWeekStart = NR;
        } else {
            harvestEvalInfos.harvestConfirmWeek = GetWeekFromDate(harvestConfirmWeekStart);
            harvestEvalInfos.ttHarvestConfirmWeekStart = harvestConfirmWeekStart;
        }

        // OLD IMPLEMENTATION
//        // "HARVESTED CONDITIONS NOT DETECTED"
//        if (IsNA(harvestConfirmWeekStart) || idxFirstHarvest == -1) {
//            // report results from last available week
//            int lastAvailableIdx = retAllMergedValues.size() - 1;
//            harvestInfos.ndviPresence = retAllMergedValues[lastAvailableIdx].ndviPresence;               // M1
//            harvestInfos.candidateOptical = retAllMergedValues[lastAvailableIdx].candidateOptical;       // M2
//            harvestInfos.candidateAmplitude = retAllMergedValues[lastAvailableIdx].candidateAmplitude;   // M3
//            harvestInfos.amplitudePresence = retAllMergedValues[lastAvailableIdx].amplitudePresence;     // M4
//            harvestInfos.candidateCoherence = retAllMergedValues[lastAvailableIdx].candidateCoherence;   // M5
//            harvestInfos.harvestConfirmWeek = NR;
//            harvestInfos.ttHarvestConfirmWeekStart = NR;
//        } else {
//            harvestInfos.ndviPresence = retAllMergedValues[idxFirstHarvest].ndviPresence;                // M1
//            harvestInfos.candidateOptical = retAllMergedValues[idxFirstHarvest].candidateOptical;        // M2
//            harvestInfos.candidateAmplitude = retAllMergedValues[idxFirstHarvest].candidateAmplitude;    // M3
//            harvestInfos.amplitudePresence = retAllMergedValues[idxFirstHarvest].amplitudePresence;      // M4
//            harvestInfos.candidateCoherence = retAllMergedValues[idxFirstHarvest].candidateCoherence;    // M5
//            harvestInfos.harvestConfirmWeek = GetWeekFromDate(harvestConfirmWeekStart);
//            harvestInfos.ttHarvestConfirmWeekStart = harvestConfirmWeekStart;
//        }

        if (idxFirstS1Harvest != -1) {
            harvestEvalInfos.ttS1HarvestWeekStart = retAllMergedValues[idxFirstS1Harvest].ttDate;
        } else {
            harvestEvalInfos.ttS1HarvestWeekStart = NR;
        }

        return true;
    }

    //  TIME SERIES ANALYSIS FOR EFA PRACTICES
    bool CatchCropPracticeAnalysis(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                                        const HarvestEvaluationInfoType &harvestEvalInfos, HarvestEvaluationInfoType &ccHarvestEvalInfos) {
        time_t ttDateA;     // last possible start of catch-crop period
        time_t ttDateB;     // last possible end of catch-crop period
        time_t ttDateC;
        time_t ttDateD = 0;

        time_t weekA;
        //time_t weekB;
        time_t catchStart;

        ccHarvestEvalInfos = harvestEvalInfos;

        if (harvestEvalInfos.ttPracticeEndTime == 0) {
            // # last possible start of catch-crop period
            ttDateA = harvestEvalInfos.ttPracticeStartTime;
            // # last possible end of catch-crop period
            ttDateB = ttDateA + (m_CatchPeriod - 1) * SEC_IN_DAY;
            weekA = FloorDateToWeekStart(harvestEvalInfos.ttPracticeStartTime);
            //weekB = FloorDateToWeekStart(ttDateB);
            if (IsNA(harvestEvalInfos.harvestConfirmWeek)) {
                 // harvest is "NR", set the start of catch-crop period to the last possible start of
                // catch-crop period (to select the NDVI values)
                catchStart = ttDateA;
            } else {
                int lastValidHarvestIdx = -1;
                for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                    // after the last possible start of catch-crop period all the weeks with
                    // the potential $harvest conditions are set to FALSE
                    if (retAllMergedValues[i].ttDate > weekA) {
                        retAllMergedValues[i].harvest = false;
                    } else {
                        // select the weeks with potential $harvest conditions (== bare land)
                        if (retAllMergedValues[i].harvest == true) {
                            lastValidHarvestIdx = i;
                        }
                    }
                }
                // set the start of catch-crop period to the last potential harvest week  or the harvest week from the harvest evaluation
                if (lastValidHarvestIdx != -1) {
                    catchStart = retAllMergedValues[lastValidHarvestIdx].ttDate;
                } else {
                    catchStart = harvestEvalInfos.ttHarvestConfirmWeekStart;
                }
            }
            // set the start of the catch-crop (first possible date)
            ttDateC = catchStart;
            time_t ttCatchPeriodStart =  0;
            if (m_CatchPeriodStart.size() > 0) {
                ttCatchPeriodStart = GetTimeFromString(m_CatchPeriodStart);
                if (ttDateC < ttCatchPeriodStart) {
                    // a variable can be used to define the earliest date the catch-crop can be sown (???)
                    ttDateC = ttCatchPeriodStart;
                }
            }

            // check if the period is covered with data
            if ((retAllMergedValues[retAllMergedValues.size()-1].ttDate - ttDateC) >= (m_CatchPeriod-1) * SEC_IN_DAY) {
                // In the original script, here it was a calculation of the EFA markers
                // but function f.efa.markers is used to get the $ndvi.mean for selected weeks <date.c; date.b>

                // TODO: When filtering, the mean values might change for the limits of the interval
//                std::vector<MergedDateAmplitudeType> mergedAmpInfos;
//                std::vector<MergedAllValInfosType> filteredAllMergedValues;
//                GroupAndMergeFilteredData(fieldInfos, ttDateC, ttDateB, mergedAmpInfos, filteredAllMergedValues);
                const std::vector<MergedAllValInfosType> &filteredAllMergedValues = FilterValuesByDates(
                            retAllMergedValues, ttDateC, ttDateB);
                // if there is any NDVI value
                // select the maximum NDVI
                double minVal, maxVal, efaMin, efaMax;
                // function f.efa.markers is used to get the $ndvi.mean for selected weeks <date.c; date.b>
                if (GetMinMaxNdviValues(filteredAllMergedValues, minVal, maxVal)) {
                    efaMax = maxVal;    // select the maximum NDVI
                    efaMin = minVal;    // select the minimum NDVI
                    if( IsLess(efaMin, m_EfaNdviDown) ){ efaMin = m_EfaNdviDown; }                 // efa.min is >= efa.ndvi.dw
                    if( IsGreater(efaMin, m_NdviUp) ){ efaMin = m_EfaNdviUp; }                 // efa.min is <= efa.ndvi.up if efa.min>ndvi.up !
                } else {
                    // else set it to efa.ndvi.dw
                    efaMax = m_EfaNdviDown;
                    efaMin = m_EfaNdviDown;
                }
                double efaChange = (efaMax - efaMin) * m_CatchProportion;                  // compute ndvi buffer
                if( efaChange <10 ) { efaChange = 10; }
                // Date from which the $candidate.efa (potential bare-land conditions) are relevant to compute
                if (!IsNA(harvestEvalInfos.harvestConfirmWeek)) {
                    ttDateD = harvestEvalInfos.ttHarvestConfirmWeekStart;
                } else {
                    // get first day of the posible harvest weeks
                    for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                        if (retAllMergedValues[i].candidateOptical) {
                            ttDateD = retAllMergedValues[i].ttDate;
                            break;
                        }
                    }
                    if (ttDateD == 0) {
                        ttDateD = ttDateC;
                    }
                }
                bool curCatchStart;
                std::vector<int> catchStartIndices;
                // Compute $candidate.efa and define the start of the catch-crop period
                double efaMinChangeVal = efaMin + efaChange;
                for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                    retAllMergedValues[i].candidateEfa = (!IsNA(retAllMergedValues[i].ndviNext)) &&
                            IsLess(retAllMergedValues[i].ndviNext, efaMinChangeVal);
                    // potential bare-land conditions (based on NDVI)
                    if (retAllMergedValues[i].ttDate > weekA) {
                        // after the last possible start of catch-crop period days all the weeks with
                        // the potential bare-land conditions are set to FALSE
                        retAllMergedValues[i].candidateEfa = false;
                    }

                    // # update
                    if (ttCatchPeriodStart > 0) {
                        time_t ttFlooredCatchPeriodStart = FloorDateToWeekStart(ttCatchPeriodStart);
                        if (retAllMergedValues[i].ttDate < ttFlooredCatchPeriodStart) {
                            // after the last possible start of catch-crop period days all the weeks with
                            // the potential bare-land conditions are set to FALSE
                            retAllMergedValues[i].candidateEfa = false;
                        }
                    }

                    // before the date.d all the weeks with the potential bare-land conditions are set to FALSE
                    if (retAllMergedValues[i].ttDate < ttDateD) {
                        retAllMergedValues[i].candidateEfa = false;
                    }
                    // define the bare-land conditions from all three time-series == breaks
                    curCatchStart = retAllMergedValues[i].candidateEfa &&
                            retAllMergedValues[i].candidateAmplitude &&
                            retAllMergedValues[i].candidateCoherence;
                    if (curCatchStart) {
                        // weeks with defined breaks
                        catchStartIndices.push_back(i);
                    }
                }
                int lastValidCatchStartIdx = -1;
                if (catchStartIndices.size() > 1) {
                   int catchStartTest = 0;
                   int catchPeriodInSec = m_CatchPeriod * SEC_IN_DAY;
                   for(size_t i = 1; i<catchStartIndices.size(); i++) {
                        catchStartTest = retAllMergedValues[catchStartIndices[i]].ttDate - retAllMergedValues[catchStartIndices[i-1]].ttDate;
                        if (catchStartTest >= catchPeriodInSec) {
                            lastValidCatchStartIdx = i - 1;
                        }
                    }
                }
                if (lastValidCatchStartIdx == -1 && catchStartIndices.size() > 0) {
                    lastValidCatchStartIdx = catchStartIndices.size()-1;
                }

                if (lastValidCatchStartIdx != -1) {
                    // last break == week of the most probalbe start of the catch-crop period
                    catchStart = retAllMergedValues[catchStartIndices[lastValidCatchStartIdx]].ttDate;
                } else {
                    catchStart = ttDateD;
                }
            } else {
                // if the period is not covered with data set the start of catch-crop period to the last possible start of catch-crop period
                catchStart = ttDateA;
            }
            if (m_CatchPeriodStart.size() > 0) {
                // a variable can be used to define the earliest start of the catch-crop period
                if (catchStart < ttCatchPeriodStart) {
                    catchStart = ttCatchPeriodStart;
                }
            }
            // set the first day of the catch-crop period
            ccHarvestEvalInfos.ttPracticeStartTime = catchStart;
            // set the last day of the catch-crop period
            ccHarvestEvalInfos.ttPracticeEndTime = catchStart + ((m_CatchPeriod - 1) * SEC_IN_DAY);
        }

        // ### EFA practice period ###
        ttDateA = ccHarvestEvalInfos.ttPracticeStartTime;
        ttDateB = ccHarvestEvalInfos.ttPracticeEndTime;
        weekA = FloorDateToWeekStart(ttDateA);
        //weekB = FloorDateToWeekStart(ttDateB);

        bool efaPeriodStarted = retAllMergedValues[retAllMergedValues.size() - 1].ttDate > weekA;
        //bool efaPeriodEnded = retAllMergedValues[retAllMergedValues.size() - 1].ttDate >= weekB;

//      DEBUG
//        std::cout << TimeToString(weekA) << std::endl;
//        std::cout << TimeToString(weekB) << std::endl;
//        std::cout << TimeToString(retAllMergedValues[retAllMergedValues.size() - 1].ttDate) << std::endl;
//      DEBUG

        // ### BEFORE end of EFA practice period ###
        if (!efaPeriodStarted) {
            // EFA practice is not evaluated before the end of the EFA practice period - return "NR" evaluation
            ccHarvestEvalInfos.efaIndex = "NR";
            ccHarvestEvalInfos.ndviPresence = NR;                        // M6
            ccHarvestEvalInfos.ndviGrowth = NR;                          // M7
            ccHarvestEvalInfos.ndviNoLoss = NR;                          // M8
            ccHarvestEvalInfos.ampNoLoss = NR;                           // M9
            ccHarvestEvalInfos.cohNoLoss = NR;                           // M10
            return true;
        }

        // ### EFA PRACTICE EVALUATION ###

        // is catch-crop grown in/under the main crop on the parcel
        bool catchInMaincrop = ((fieldInfos.practiceType == m_CatchMain) ||
                (fieldInfos.practiceType == m_CatchCropIsMain));
        time_t ttVegSeasonStart = FloorDateToWeekStart(ccHarvestEvalInfos.ttVegStartTime);

//      DEBUG
//        std::cout << TimeToString(ttVegSeasonStart) << std::endl;
//      DEBUG

        // is there any evidence of main crop (>opt.thr.vegcycle) in the vegetation season
        bool vegSeasonStatus = false;
        for (size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (!IsNA(retAllMergedValues[i].vegWeeks) && retAllMergedValues[i].vegWeeks == true &&
                    IsGreater(retAllMergedValues[i].ndviMeanVal, m_OpticalThrVegCycle)) {
                vegSeasonStatus = true;
                break;
            }
        }
        if (!vegSeasonStatus) {
            // # no evidence of the main crop vegetation in the vegetation season - evaluation of catch-crop is not relevant - return "NR"
            ccHarvestEvalInfos.efaIndex = "NR";
            ccHarvestEvalInfos.ndviPresence = NR;                        // M6
            ccHarvestEvalInfos.ndviGrowth = NR;                          // M7
            ccHarvestEvalInfos.ndviNoLoss = NR;                          // M8
            ccHarvestEvalInfos.ampNoLoss = NR;                           // M9
            ccHarvestEvalInfos.cohNoLoss = NR;                           // M10
            return true;

        } else if (IsNA(ccHarvestEvalInfos.harvestConfirmWeek)) {
            if (!catchInMaincrop) {
                bool found = false;
                for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                    if (retAllMergedValues[i].ttDate >= ttVegSeasonStart && retAllMergedValues[i].ttDate <= weekA) {
                        if (retAllMergedValues[i].ndviDrop == true) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    ccHarvestEvalInfos.efaIndex = "POOR";
                    ccHarvestEvalInfos.ndviPresence = NR;                        // M6
                    ccHarvestEvalInfos.ndviGrowth = NR;                          // M7
                    ccHarvestEvalInfos.ndviNoLoss = NR;                          // M8
                    ccHarvestEvalInfos.ampNoLoss = NR;                           // M9
                    ccHarvestEvalInfos.cohNoLoss = NR;                           // M10
                    return true;
                } //else {
                    // harvest was not detected but there is "drop" in NDVI before the catch-crop period - evaluate
                // }
            } //else {
                // harvest was not detected but catch-crop was sown in/under the main crop - evaluate
            // }
        } else {
            if (ccHarvestEvalInfos.ttHarvestConfirmWeekStart > weekA && !catchInMaincrop) {
                ccHarvestEvalInfos.efaIndex = "POOR";
                ccHarvestEvalInfos.ndviPresence = NR;                        // M6
                ccHarvestEvalInfos.ndviGrowth = NR;                          // M7
                ccHarvestEvalInfos.ndviNoLoss = NR;                          // M8
                ccHarvestEvalInfos.ampNoLoss = NR;                           // M9
                ccHarvestEvalInfos.cohNoLoss = NR;                           // M10
                return true;
            }
        }

        return CCEfaMarkersExtraction(ttDateA, ttDateB, weekA, fieldInfos, ccHarvestEvalInfos);
    }

    bool CCEfaMarkersExtraction(time_t ttDateA, time_t ttDateB, time_t weekA,
                             const FieldInfoType &fieldInfos,
                             HarvestEvaluationInfoType &ccHarvestEvalInfos) {
        // get efa markers for the defined catch-crop period
        std::vector<EfaMarkersInfoType> efaMarkers;
        ExtractEfaMarkers(ttDateA, ttDateB, fieldInfos, efaMarkers);

        // ### MARKER 6 - efa.presence.ndvi ###
        // # if there is evidence of NDVI>thr in the catch-crop period - M6 is TRUE
        EfaMarkersInfoType efa;
        efa.ndviPresence = IsNdviPresence(efaMarkers);

        // # if there is no NDVI value from third week of EFA practice period and M6 is FALSE,
        //   set the M6 to NA (low values are allowed at the beginning of the period)
        if (!efa.ndviPresence) {
            bool allNdviNA = true;
            for(size_t i = 0; i<efaMarkers.size(); i++) {
                if (efaMarkers[i].ttDate >= weekA + 21 * SEC_IN_DAY) {
                    if (!IsNA(efaMarkers[i].ndviMean)) {
                        allNdviNA = false;
                        break;
                    }
                }
            }
            if (allNdviNA) {
                efa.ndviPresence = NOT_AVAILABLE;
            }
        }

        // ### MARKER 7 - efa.growth.ndvi ###
        // # if all the NDVI are >thr from third week of EFA practice period - M7 is TRUE
        bool allNdviGrowthOK = true;
        bool allNdviGrowthNA = true;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (efaMarkers[i].ttDate >= weekA + 21 * SEC_IN_DAY) {
                if (!IsNA(efaMarkers[i].ndviGrowth)) {
                    allNdviGrowthNA = false;
                    if (efaMarkers[i].ndviGrowth == false) {
                        allNdviGrowthOK = false;
                        break;
                    }
                }
            }
        }
        efa.ndviGrowth = allNdviGrowthOK;

        if (IsNA(efa.ndviPresence) || allNdviGrowthNA) {
            efa.ndviGrowth = NOT_AVAILABLE;
        }

        // ### MARKER 8 - efa.noloss.ndvi ###
        if (IsNA(efa.ndviPresence)) {
            efa.ndviNoLoss = NOT_AVAILABLE;
        } else {
            bool allNoLossNA = true;
            bool allNoLossTrue = true;
            for(size_t i = 0; i<efaMarkers.size(); i++) {
                if (!IsNA(efaMarkers[i].ndviNoLoss)) {
                    allNoLossNA = false;
                    if (efaMarkers[i].ndviNoLoss == false) {
                        allNoLossTrue = false;
                        break;
                    }
                }
            }
            if (allNoLossNA) {
                efa.ndviNoLoss = NOT_AVAILABLE;
            } else {
                efa.ndviNoLoss = allNoLossTrue;
            }
        }

        // ### NDVI markers evaluation ###
        // # evaluate the NDVI markers (M6,M7,M8)
        short efaNdvi;
        if (IsNA(efa.ndviPresence) && IsNA(efa.ndviGrowth) && IsNA(efa.ndviNoLoss)) {
            efaNdvi = NOT_AVAILABLE;
        } else {
            // if not all flags are true
            efaNdvi = true;
            if(efa.ndviPresence == false || efa.ndviNoLoss == false || efa.ndviGrowth == false) {
                efaNdvi = false;
            }
        }

        // ### MARKER 9 - backscatter no loss - efa.grd ###
        // # if there is no loss in backscatter - M9 is TRUE
        bool allAmpLossNA = true;
        bool allAmpLossTrue = true;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (!IsNA(efaMarkers[i].ampNoLoss)) {
                allAmpLossNA = false;
                if (efaMarkers[i].ampNoLoss == false) {
                    allAmpLossTrue = false;
                    break;
                }
            }
        }
        efa.ampNoLoss = allAmpLossNA ? NOT_AVAILABLE : allAmpLossTrue;

        // ### MARKER 10 - coherence no loss - efa.coh ###
        // # if there is no loss in coherence from third week of the EFA practice period - M10 is TRUE
        bool allCohLossTrue = true;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (efaMarkers[i].ttDate >= weekA + 14 * SEC_IN_DAY) {
                if (!IsNA(efaMarkers[i].cohNoLoss)) {
                    if (efaMarkers[i].cohNoLoss == false) {
                        allCohLossTrue = false;
                        break;
                    }
                }
            }
        }
        efa.cohNoLoss = allCohLossTrue;

        // number the FALSE markers
        int efaNotCompliant = 0;
        efaNotCompliant += (efa.ndviPresence == false ? 1 : 0);
        efaNotCompliant += (efa.ndviGrowth == false ? 1 : 0);
        efaNotCompliant += (efa.ndviNoLoss == false ? 1 : 0);
        efaNotCompliant += (efa.ampNoLoss == false ? 1 : 0);
        efaNotCompliant += (efa.cohNoLoss == false ? 1 : 0);

        // # no NDVI values in the catch-crop perid - evaluation is based only on SAR markers (M9,M10)
        if (IsNA(efaNdvi)) {
            if (efaNotCompliant == 0) {
                ccHarvestEvalInfos.efaIndex = "MODERATE";
            } else {
                ccHarvestEvalInfos.efaIndex = "WEAK";
            }
        } else {
            switch (efaNotCompliant) {
                case 0:
                    ccHarvestEvalInfos.efaIndex = "STRONG";
                    break;
                case 1:
                    ccHarvestEvalInfos.efaIndex = "MODERATE";
                    break;
                case 2:
                    ccHarvestEvalInfos.efaIndex = "WEAK";
                    break;
                default:
                    ccHarvestEvalInfos.efaIndex = "POOR";
                    break;
            }
        }
        ccHarvestEvalInfos.ndviPresence = efa.ndviPresence;
        ccHarvestEvalInfos.ndviGrowth = efa.ndviGrowth;
        ccHarvestEvalInfos.ndviNoLoss = efa.ndviNoLoss;
        ccHarvestEvalInfos.ampNoLoss = efa.ampNoLoss;
        ccHarvestEvalInfos.cohNoLoss = efa.cohNoLoss;

        return true;
    }

    bool FallowLandPracticeAnalysis(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                                        HarvestEvaluationInfoType &harvestInfos, HarvestEvaluationInfoType &flHarvestEvalInfos) {
        flHarvestEvalInfos = harvestInfos;

        flHarvestEvalInfos.ndviPresence = NR;                        // M6
        flHarvestEvalInfos.ndviGrowth = NR;                          // M7
        flHarvestEvalInfos.ndviNoLoss = NR;                          // M8
        flHarvestEvalInfos.ampNoLoss = NR;                           // M9
        flHarvestEvalInfos.cohNoLoss = NR;                           // M10

        time_t ttDateA = fieldInfos.ttPracticeStartTime;     // last possible start of catch-crop period
        time_t ttDateB = fieldInfos.ttPracticeEndTime;     // last possible end of catch-crop period
        time_t weekA = fieldInfos.ttPracticeStartWeekFloorTime;
        time_t weekB = fieldInfos.ttPracticeEndWeekFloorTime;

        time_t lastDate = retAllMergedValues[retAllMergedValues.size()-1].ttDate;
        bool efaPeriodStarted = (lastDate > weekA);
        //bool efaPeriodEnded = (lastDate >= weekB);

        // ### BEFORE end of EFA practice period ###
        if (!efaPeriodStarted) {
            // # EFA practice is not evaluated before the end of the EFA practice period - return "NR" evaluation
            flHarvestEvalInfos.efaIndex = "NR";
            return true;
        }

        // ### EFA PRACTICE EVALUATION ###
        // get efa markers for the defined catch-crop period
        std::vector<EfaMarkersInfoType> efaMarkers;
        ExtractEfaMarkers(ttDateA, ttDateB, fieldInfos, efaMarkers);

        // ### MARKER 6 - efa.presence.ndvi ###
        // # if there is evidence of vegetation (NDVI>efa.ndvi.thr) in the EFA practice period - M6 is TRUE
        flHarvestEvalInfos.ndviPresence = IsNdviPresence(efaMarkers);

        if (fieldInfos.countryCode == "LTU") {
            // for Lithuania there is a special case
            bool efaCoh = false;
            //time_t ttCohStableWeek = 0;
            if (efaMarkers.size() >= 9) {
                // is there loss in coherence (FALSE value) in 9 subsequent weeks
                int cohStableCnt = 0;
                for (int i = (efaMarkers.size() - 1); i>= 0; i--) {
                    if (efaMarkers[i].cohNoLoss == true) {
                        cohStableCnt++;
                        if (cohStableCnt >= 9) {
                            efaCoh = true;
                            //ttCohStableWeek = efaMarkers[i].ttDate;
                            break;
                        }
                    } else {
                        cohStableCnt = 0;
                    }
                }
            }
            if (fieldInfos.practiceType == "PDJ") {
                if (!efaCoh) {
                    flHarvestEvalInfos.efaIndex = "STRONG";
                    // # harvest is not evaluated for the confirmed black fallow as it is not expected
                    harvestInfos.harvestConfirmWeek = NR;
                    harvestInfos.ttHarvestConfirmWeekStart = NR;
                    flHarvestEvalInfos.harvestConfirmWeek = NR;
                    flHarvestEvalInfos.ttHarvestConfirmWeekStart = NR;
                } else {
                    // # if there is no-loss of coherence in all the 9 subsequent
                    // weeks (if M10 is TRUE) - return WEAK evaluation for the black fallow practice
                    flHarvestEvalInfos.efaIndex = "WEAK";

                    // NOTE: Code Just for testing in the R code.
                    //flHarvestInfos.ttPracticeStartTime = ttCohStableWeek;
                    //flHarvestInfos.ttPracticeEndTime = ttCohStableWeek + 62 * SEC_IN_DAY;
                }
                // M6 is not used - return "NR"
                flHarvestEvalInfos.ndviPresence = NR;
            } else if (fieldInfos.practiceType == "PDZ") {
                // ### GREEN-FALLOW EVALUATION ###
                if (!efaCoh) {
                    // # for the green fallow practice no-loss of coherence in at least 9 subsequent weeks is expected
                    // # if there is a loss in all the 9 subsequent weeks (if M10 is FALSE) - return WEAK evaluation
                    // # in such case harvest is not evaluated
                    flHarvestEvalInfos.efaIndex = "WEAK";
                    //flHarvestInfos.harvestConfirmWeek = NR;
                    //flHarvestInfos.ttHarvestConfirmWeekStart = NR;
                } else if (flHarvestEvalInfos.ndviPresence == false) {
                    // # if there is no evidence of vegetation in NDVI - return WEAK evaluation for the green fallow practice
                    // # green fallow shall be inserted to soil up to the end of the efa period - harvest shall be detected
                    // # if harvest is not detected - return WEAK evaluation
                    flHarvestEvalInfos.efaIndex = "WEAK";
                } else if (IsNA(flHarvestEvalInfos.harvestConfirmWeek)) {
                    flHarvestEvalInfos.efaIndex = "MODERATE";
                } else {
                    // # if harvest is detected
                    // # set the first day of the harvest week
                    if (flHarvestEvalInfos.ttHarvestConfirmWeekStart <= (weekB - 62 * SEC_IN_DAY)) {
                        // # too early - return MODERATE evaluation
                        flHarvestEvalInfos.efaIndex = "MODERATE";
                    } else if (flHarvestEvalInfos.ttHarvestConfirmWeekStart <= (weekB + 7 * SEC_IN_DAY)) {
                        // # before the end of the efa period - return STRONG evaluation
                        flHarvestEvalInfos.efaIndex = "STRONG";
                    } else {
                        // # too late - return WEAK evaluation
                        flHarvestEvalInfos.efaIndex = "MODERATE";
                    }
                }

                // # if there is no NDVI value in the whole EFA practice period - return "NA" for the M6
                if (AllNdviMeanAreNA(efaMarkers)) {
                    flHarvestEvalInfos.ndviPresence = NOT_AVAILABLE;
                }
            }
            flHarvestEvalInfos.cohNoLoss = efaCoh;                       // M10
        } else {
            if (fieldInfos.countryCode == "CZE") {
                time_t ttDateC = GetTimeFromString(m_flMarkersStartDateStr);
                time_t ttDateD = GetTimeFromString(m_flMarkersEndDateStr);
                time_t weekC = FloorDateToWeekStart(ttDateC);
                time_t weekD = FloorDateToWeekStart(ttDateD);

                if (!flHarvestEvalInfos.ndviPresence) {
                    // # no evidence of GREEN FALLOW vegetation in the EFA practice period - return POOR evaluation
                    flHarvestEvalInfos.efaIndex = "POOR";
                    return true;

                } else if (!IsNA(harvestInfos.harvestConfirmWeek)) {
                    // # set the first day of the harvest week - evaluate
                    time_t harvestConfirmed = harvestInfos.ttHarvestConfirmWeekStart;
                    if (harvestConfirmed > weekA && harvestConfirmed <= weekB) {
                        // # harvest was detected within the fallow period - FALLOW is considered not ok - return WEAK evaluation
                        flHarvestEvalInfos.efaIndex = "WEAK";
                        return true;
                    }
                } // else {
                        // # vegetation is present, harvest was not detected - FALLOW is considered ok - evaluate
                  // }

                // ### MARKER 8 - efa.noloss.ndvi ###
                // # mowing or mulching is required between "2017-06-01" and "2017-08-31" - efa.noloss.ndvi shall be FALSE in this period
                std::vector<EfaMarkersInfoType> efaMarkers2;
                // # get efa markers for the specified period & buffer
                ExtractEfaMarkers(ttDateC - 30 * SEC_IN_DAY, ttDateD + 30 * SEC_IN_DAY, fieldInfos, efaMarkers2);
                for (int i = (int)efaMarkers2.size() - 2; i>=0; i--) {
                    if (IsNA(efaMarkers2[i].ndviNoLoss) && !IsNA(efaMarkers2[i+1].ndviNoLoss)) {
                        efaMarkers2[i].ndviNoLoss = efaMarkers2[i+1].ndviNoLoss;
                    }
                }

                // # if there is evidence of loss in NDVI in the period - M8(noloss) is FALSE
                bool ndviNoLossFalseFound = false;
                for (size_t i = 0; i<efaMarkers2.size(); i++) {
                    if (IsNA(efaMarkers2[i].ndviNoLoss)) {
                        continue;
                    }
                    if ((efaMarkers2[i].ttDate >= weekC) && (efaMarkers2[i].ttDate <= (weekD + SEC_IN_WEEK))) {
                        if (efaMarkers2[i].ndviNoLoss == false) {
                            ndviNoLossFalseFound = true;
                            break;
                        }
                    }
                }
                flHarvestEvalInfos.ndviNoLoss = !ndviNoLossFalseFound; // M8
                if (!flHarvestEvalInfos.ndviNoLoss) {
                    flHarvestEvalInfos.efaIndex = "STRONG";
                } else {
                    flHarvestEvalInfos.efaIndex = "MODERATE";
                }
            } else {
                // ITA or ESP
                time_t weekC = weekB;
                if (fieldInfos.countryCode == "ITA") {
                    time_t ttDateC = GetTimeFromString(m_flMarkersStartDateStr);
                    weekC = FloorDateToWeekStart(ttDateC);
                }
                if (!flHarvestEvalInfos.ndviPresence) {
                    // # no evidence of vegetation in the whole vegetation season (==black fallow) - return STRONG evaluation
                    flHarvestEvalInfos.efaIndex = "STRONG";
                } else if (IsNA(harvestInfos.harvestConfirmWeek)) {
                    flHarvestEvalInfos.efaIndex = "MODERATE";
                } else {
                    // # set the first day of the harvest week - evaluate
                    time_t harvestConfirmed = harvestInfos.ttHarvestConfirmWeekStart;
                    if (harvestConfirmed > weekA && harvestConfirmed <= weekC) {
                        // # harvest was detected within the fallow period - FALLOW is considered not ok - return WEAK evaluation
                        flHarvestEvalInfos.efaIndex = "WEAK";
                    } else {
                        flHarvestEvalInfos.efaIndex = "MODERATE";
                    }
                }
                // # if there is no NDVI value in the whole EFA practice period - return "NA" for the M6
                if (AllNdviMeanAreNA(efaMarkers)) {
                    flHarvestEvalInfos.ndviPresence = NOT_AVAILABLE;
                }
            }
        }
        return true;
    }

    bool NitrogenFixingCropPracticeAnalysis(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                                        const HarvestEvaluationInfoType &harvestInfos, HarvestEvaluationInfoType &ncHarvestEvalInfos) {
        ncHarvestEvalInfos = harvestInfos;

        ncHarvestEvalInfos.ndviPresence = NR;                        // M6
        ncHarvestEvalInfos.ndviGrowth = NR;                          // M7
        ncHarvestEvalInfos.ndviNoLoss = NR;                          // M8
        ncHarvestEvalInfos.ampNoLoss = NR;                           // M9
        ncHarvestEvalInfos.cohNoLoss = NR;                           // M10

        time_t ttDateA = fieldInfos.ttPracticeStartTime;     // last possible start of catch-crop period
        time_t ttDateB = fieldInfos.ttPracticeEndTime;     // last possible end of catch-crop period
        time_t weekA = fieldInfos.ttPracticeStartWeekFloorTime;
        time_t weekB = fieldInfos.ttPracticeEndWeekFloorTime;

        time_t lastDate = retAllMergedValues[retAllMergedValues.size()-1].ttDate;
        bool efaPeriodStarted = (lastDate > weekA);
        //bool efaPeriodEnded = (lastDate >= weekB);

        // ### BEFORE end of EFA practice period ###
        if (!efaPeriodStarted) {
            // # EFA practice is not evaluated before the end of the EFA practice period - return "NR" evaluation
            ncHarvestEvalInfos.efaIndex = "NR";
            return true;
        }

        // ### EFA PRACTICE EVALUATION ###
        // get efa markers for the defined catch-crop period
        time_t vegSeasonStart = (fieldInfos.countryCode == "CZE" ?
                    FloorDateToWeekStart(harvestInfos.ttVegStartTime):
                    ttDateA);
        std::vector<EfaMarkersInfoType> efaMarkers;
        ExtractEfaMarkers(vegSeasonStart, ttDateB, fieldInfos, efaMarkers);

        // ### MARKER 6 - efa.presence.ndvi ###
        // # if there is evidence of vegetation (NDVI>efa.ndvi.thr) in the vegetation season - M6 is TRUE
        ncHarvestEvalInfos.ndviPresence = IsNdviPresence(efaMarkers);

        if (fieldInfos.countryCode != "CZE") {
            if (!ncHarvestEvalInfos.ndviPresence) {
                // # no evidence of vegetation in the whole vegetation season - no evidence of NFC - return WEAK evaluation
                if (fieldInfos.countryCode == "ESP") {
                    ncHarvestEvalInfos.efaIndex = "POOR";
                } else {
                    ncHarvestEvalInfos.efaIndex = "WEAK";
                }
            } else if(IsNA(ncHarvestEvalInfos.harvestConfirmWeek)) {
                // vegetation is present, harvest was not detected - NFC is considered ok - return MODERATE evaluation
                ncHarvestEvalInfos.efaIndex = "MODERATE";
            } else {
                // # set the first day of the harvest week
                //# vegetation is present, harvest was detected - NFC is considered ok - return STRONG evaluation
                if ((fieldInfos.countryCode == "ROU") &&
                        (harvestInfos.ttHarvestConfirmWeekStart > weekA &&
                        harvestInfos.ttHarvestConfirmWeekStart < weekB)) {
                    ncHarvestEvalInfos.efaIndex = "WEAK";
                } else {
                    ncHarvestEvalInfos.efaIndex = "STRONG";
                }
            }
            // # if there is no NDVI value in the EFA practice period - M6 is NA
            if (AllNdviMeanAreNA(efaMarkers)) {
                ncHarvestEvalInfos.ndviPresence = NOT_AVAILABLE;
            }
            if (fieldInfos.countryCode == "ROU") {
                if (IsNA(ncHarvestEvalInfos.ndviPresence)) {
                    ncHarvestEvalInfos.efaIndex = "NR";
                }
            }
        } else {
            if (!ncHarvestEvalInfos.ndviPresence) {
                // # no evidence of the NFC vegetation in the vegetation season - return POOR evaluation
                ncHarvestEvalInfos.efaIndex = "POOR";
                return true;

            } else if (!IsNA(harvestInfos.harvestConfirmWeek)) {
                // # set the first day of the harvest week - evaluate
                time_t harvestConfirmed = harvestInfos.ttHarvestConfirmWeekStart;
                if (harvestConfirmed > weekA && harvestConfirmed <= weekB) {
                    // # harvest detected within the efa practice period - NFC is considered not ok - return WEAK evaluation
                    ncHarvestEvalInfos.efaIndex = "WEAK";
                    return true;
                }
            } //else {
                // # vegetation is present, harvest was not detected - NFC is considered ok - evaluate
            // }

            // ### MARKER 6 - efa.presence.ndvi ###
            std::vector<EfaMarkersInfoType> efaMarkers2;
            // # get efa markers for the specified period & buffer
            ExtractEfaMarkers(ttDateA, ttDateB, fieldInfos, efaMarkers2);

            // #  if there is evidence of vegetation (NDVI>efa.ndvi.thr) in the EFA practice period - M6 is TRUE
            ncHarvestEvalInfos.ndviPresence = IsNdviPresence(efaMarkers2);
            if (ncHarvestEvalInfos.ndviPresence) {
                ncHarvestEvalInfos.efaIndex = "STRONG";
            } else {
                ncHarvestEvalInfos.efaIndex = "MODERATE";
            }
        }
        return true;
    }

    bool ExtractEfaMarkers(time_t ttStartTime, time_t ttEndTime, const FieldInfoType &fieldInfos,
                           std::vector<EfaMarkersInfoType> &efaMarkers) {

        std::vector<MergedAllValInfosType> allMergedValues;
        std::vector<MergedDateAmplitudeType> mergedAmpInfos;
        if (!GroupAndMergeFilteredData(fieldInfos, ttStartTime, ttEndTime, mergedAmpInfos, allMergedValues)) {
            return false;
        }

        efaMarkers.resize(allMergedValues.size());

//      DEBUG
        // std::cout << TimeToString(ttStartTime) << std::endl;
        // std::cout << TimeToString(ttEndTime) << std::endl;
        m_debugPrinter.PrintEfaMarkers(allMergedValues, efaMarkers);
//      DEBUG

        // ### COMPUTE EFA MARKERS ###

        // ### MARKER 6 - $ndvi.presence ###

        int prevNotNaNdviIdx = -1;
        bool allNdviLessDw = true;
        bool ndviLessDwFound = false;
        bool ndviLessUpFound = false;
        // TODO: Check if duplicated code with UpdateMarker2Infos
        double minMeanNdviDropVal = NOT_AVAILABLE;

        double efaNdviUp = IsNA(m_EfaNdviUp) ? m_NdviUp : m_EfaNdviUp;
        double efaNdviDown = IsNA(m_EfaNdviDown) ? m_NdviDown : m_EfaNdviDown;
        double efaNdviMin = IsNA(m_EfaNdviMin) ? DEFAULT_EFA_NDVI_MIN : m_EfaNdviMin;

        // TODO: in the code below we might have access violations due to ndviCoheFilteredValues and ampFilteredValues different sizes
        for (size_t i = 0; i<allMergedValues.size(); i++) {
            efaMarkers[i].ttDate = allMergedValues[i].ttDate;
            efaMarkers[i].ndviPresence = NOT_AVAILABLE;
            efaMarkers[i].ndviDrop = NOT_AVAILABLE;
            efaMarkers[i].ndviGrowth = NOT_AVAILABLE;
            efaMarkers[i].ndviNoLoss = NOT_AVAILABLE;
            efaMarkers[i].ndviMean = allMergedValues[i].ndviMeanVal;
            if (IsNA(efaMarkers[i].ndviMean)) {
                continue;
            }
            efaMarkers[i].ndviPresence = IsGreaterOrEqual(allMergedValues[i].ndviMeanVal, m_EfaNdviThr);
            if (!IsNA(efaNdviDown) && !IsNA(efaNdviUp)) {
                if (!IsNA(allMergedValues[i].ndviMeanVal)) {
                    // skip the first line
                    if (prevNotNaNdviIdx >= 0) {
                        efaMarkers[i].ndviDrop = (IsLess(allMergedValues[i].ndviMeanVal, allMergedValues[prevNotNaNdviIdx].ndviMeanVal) &&
                                                  IsLess(allMergedValues[i].ndviMeanVal, efaNdviUp));
                    }
                    prevNotNaNdviIdx = i;
                    // not all ndvi are less than ndvi down
                    if (IsGreaterOrEqual(allMergedValues[i].ndviMeanVal, efaNdviDown)) {
                        allNdviLessDw = false;
                    }
                    if (efaMarkers[i].ndviDrop == true) {
                        if (IsLess(allMergedValues[i].ndviMeanVal, efaNdviDown)) {
                            // at least one ndvi is less than ndvi down
                            ndviLessDwFound = true;
                        }
                        if (IsLess(allMergedValues[i].ndviMeanVal, efaNdviUp)) {
                            ndviLessUpFound = true;
                        }

                        if (IsNA(minMeanNdviDropVal)) {
                            minMeanNdviDropVal = allMergedValues[i].ndviMeanVal;
                        } else {
                            if (IsGreater(minMeanNdviDropVal, allMergedValues[i].ndviMeanVal)) {
                                minMeanNdviDropVal = allMergedValues[i].ndviMeanVal;
                            }
                        }
                    }
                }
            }
            // # MARKER 7 - $ndvi.growth ###
            // if both ndviPresence and ndviDrop are NA, we let ndviGrowth to NA
            if (!IsNA(efaMarkers[i].ndviPresence) || !IsNA(efaMarkers[i].ndviDrop)) {
                efaMarkers[i].ndviGrowth = efaMarkers[i].ndviPresence || efaMarkers[i].ndviDrop == false;
            }
            if (IsNA(efaMarkers[i].ndviDrop) && (efaMarkers[i].ndviPresence == false)) {
                efaMarkers[i].ndviGrowth = false;
            }
            if (efaMarkers[i].ndviGrowth == true) {
                efaMarkers[i].ndviGrowth = IsGreaterOrEqual(allMergedValues[i].ndviMeanVal, efaNdviMin);
            }

            double opticalThresholdValue;
            if (allNdviLessDw || ndviLessDwFound) {
                opticalThresholdValue = efaNdviDown;
            } else if (ndviLessUpFound) {
                opticalThresholdValue = m_ndviStep * std::ceil(minMeanNdviDropVal / m_ndviStep);
            } else {
                opticalThresholdValue = efaNdviUp;
            }

            // ### MARKER 8 - $ndvi.noloss ###
            efaMarkers[i].ndviNoLoss = !(efaMarkers[i].ndviDrop &&
                                         IsLessOrEqual(allMergedValues[i].ndviMeanVal, opticalThresholdValue));
            if (IsNA(efaMarkers[i].ndviDrop)) {
                efaMarkers[i].ndviNoLoss = NOT_AVAILABLE;
            }
        }

        // ### MARKER 9 - $grd.noloss ###
//      DEBUG
//        m_debugPrinter.PrintEfaMarkers(allMergedValues, efaMarkers);
//      DEBUG

/*
        const std::vector<MergedDateAmplitude> &ampFilteredRawValues = FilterValuesByDates(
                    mergedAmpInfos, ttStartTime, ttEndTime);

        // extract the weeks array
        std::vector<int> weeks;
        weeks.reserve(ampFilteredRawValues.size());
        std::transform(ampFilteredRawValues.begin(), ampFilteredRawValues.end(),
                       std::back_inserter(weeks), [](MergedDateAmplitude f){return f.vvInfo.weekNo;});

        // sort the values
        std::sort(weeks.begin(), weeks.end());
        // keep the unique weeks values
        weeks.erase(std::unique(weeks.begin(), weeks.end()), weeks.end());


        int minWeek = NOT_AVAILABLE;
        int maxWeek = NOT_AVAILABLE;
        if (weeks.size() > 0) {
            minWeek = weeks[0] + 1;
            maxWeek = weeks[weeks.size() - 1] - 1;
        }
        // it might be due to the fact is the first week of the year
        if (maxWeek == -1 || IsNA(maxWeek)) {
            maxWeek = GetWeekFromDate(allMergedValues[allMergedValues.size()-1].ttDate) - 1;
        }
*/
        std::vector<int> allWeeks;
        allWeeks.reserve(mergedAmpInfos.size());
        std::transform(mergedAmpInfos.begin(), mergedAmpInfos.end(),
                       std::back_inserter(allWeeks), [](MergedDateAmplitude f){return f.vvInfo.weekNo;});
        // sort the values
        std::vector<int> allWeeksUniques = allWeeks;
        std::sort(allWeeksUniques.begin(), allWeeksUniques.end());
        // keep the unique weeks values
        allWeeksUniques.erase(std::unique(allWeeksUniques.begin(), allWeeksUniques.end()), allWeeksUniques.end());

        std::vector<double> ampSlopeVec, ampPValueVec;
        ampSlopeVec.resize(efaMarkers.size());
        std::fill (ampSlopeVec.begin(),ampSlopeVec.end(), NOT_AVAILABLE);
        ampPValueVec.resize(efaMarkers.size());
        std::fill (ampPValueVec.begin(),ampPValueVec.end(), NOT_AVAILABLE);

        if (allWeeksUniques.size() >= 3) {
            // linear fitting for +/- 2 weeks
            int curWeek;
            for (size_t i = 1; i< efaMarkers.size() - 1; i++) {
                curWeek = GetWeekFromDate(efaMarkers[i].ttDate);
                //curWeek = i;
                // extract from the amplitude vectors , the weeks
                std::vector<double> subsetAmpTimes;
                std::vector<int> subsetAmpWeeks;
                std::vector<double> subsetAmpValues;
                for(size_t j = 0; j<mergedAmpInfos.size(); j++) {
                    if ((mergedAmpInfos[j].vvInfo.weekNo >= curWeek - 2) &&
                        (mergedAmpInfos[j].vvInfo.weekNo <= curWeek + 2)) {
                        if (std::find(subsetAmpWeeks.begin(), subsetAmpWeeks.end(),
                                      mergedAmpInfos[j].vvInfo.weekNo) == subsetAmpWeeks.end()) {
                            subsetAmpWeeks.push_back(mergedAmpInfos[j].vvInfo.weekNo);
                        }
                        subsetAmpTimes.push_back(mergedAmpInfos[j].ttDate / SEC_IN_DAY);
                        subsetAmpValues.push_back(mergedAmpInfos[j].ampRatio);
                    }
                }
                // we check the unique weeks
                if (subsetAmpWeeks.size() < 3) {
                    continue;
                }

//                std::cout << "DMR DATE" << std::endl;
//                for (size_t k = 0; k < subsetAmpTimes.size(); k++) {
//                    std::cout << TimeToString(subsetAmpTimes[k] * SEC_IN_DAY) << " ";
//                }
//                std::cout << std::endl;
//                std::cout << "DMR VALUE" << std::endl;
//                for (size_t k = 0; k < subsetAmpValues.size(); k++) {
//                    std::cout << ValueToString(subsetAmpValues[k]) << " ";
//                }
//                std::cout << std::endl;

                double slope;
                bool res = ComputeSlope(subsetAmpTimes, subsetAmpValues, slope);
                // std::cout << "Slope: " << slope << std::endl;
                if (res) {
                    // compute the marker to be updated
//                    int idx = curWeek - minWeek + 1;
//                    if (idx >= 0 && idx < (int)ampSlopeVec.size()) {
//                        ampSlopeVec[idx] = slope;
//                    }
                     ampSlopeVec[i] = slope;
                }

                double pValue;
                res = ComputePValue(subsetAmpTimes, subsetAmpValues, pValue);
                // std::cout << "p-Value: " << pValue << std::endl;
                if (res) {
                    // compute the marker to be updated
//                    int idx = curWeek - minWeek + 1;
//                    if (idx >= 0 && idx < (int)ampPValueVec.size()) {
//                        ampPValueVec[idx] = pValue;
//                    }
                    ampPValueVec[i] = pValue;
                }


            }
        }

        double slopeThreshold = (!IsNA(m_EfaAmpThr)) ? m_EfaAmpThr : 0.01;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (IsNA(ampSlopeVec[i]) || IsNA(ampPValueVec[i])) {
                efaMarkers[i].ampNoLoss = NOT_AVAILABLE;
            } else {
                efaMarkers[i].ampNoLoss = !(IsGreaterOrEqual(ampSlopeVec[i], slopeThreshold) &&
                                            IsLess(ampPValueVec[i], 0.05));
            }
        }

        // ### MARKER 10 - $coh.noloss ###
        double cohThrHigh = (!IsNA(m_EfaCohChange)) ? m_EfaCohChange : m_CohThrHigh;
        double cohThrAbs = (!IsNA(m_EfaCohValue)) ? m_EfaCohValue : m_CohThrAbs;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            bool cohHigh = IsGreaterOrEqual(allMergedValues[i].cohChange, cohThrHigh);
            bool cohPresence = IsGreaterOrEqual(allMergedValues[i].cohMax, cohThrAbs);
            efaMarkers[i].cohNoLoss = !(cohHigh || cohPresence);
        }

//      DEBUG
        m_debugPrinter.PrintEfaMarkers(allMergedValues, efaMarkers);
//      DEBUG

        return true;
    }

    bool IsNdviPresence(const std::vector<EfaMarkersInfoType> &efaMarkers) {
        bool ndviPresence = false;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            //if (!IsNA(efaMarkers[i].ndviPresence) && efaMarkers[i].ndviPresence == true) {
            if (efaMarkers[i].ndviPresence == true) {
                ndviPresence = true;
                break;
            }
        }
        return ndviPresence;
    }

    bool AllNdviMeanAreNA(const std::vector<EfaMarkersInfoType> &efaMarkers) {
        bool allNdviMeanAreNA = true;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (!IsNA(efaMarkers[i].ndviMean)) {
                allNdviMeanAreNA = false;
                break;
            }
        }
        return allNdviMeanAreNA;
    }

    bool GetMinMaxNdviValues(const std::vector<MergedAllValInfosType> &values, double &minVal, double &maxVal) {
        bool hasValidNdvi = false;
        double curMinVal = NOT_AVAILABLE;
        double curMaxVal = NOT_AVAILABLE;
        for(size_t i = 0; i<values.size(); i++) {
            if (IsNA(values[i].ndviMeanVal)) {
                continue;
            }
            hasValidNdvi = true;
            if (IsNA(curMinVal)) {
                curMinVal = values[i].ndviMeanVal;
                curMaxVal = curMinVal;
            } else {
                if (IsGreater(curMinVal, values[i].ndviMeanVal)) {
                    curMinVal = values[i].ndviMeanVal;
                }
                if (IsLess(curMaxVal, values[i].ndviMeanVal)) {
                    curMaxVal = values[i].ndviMeanVal;
                }
            }
        }
        minVal = curMinVal;
        maxVal = curMaxVal;

        return hasValidNdvi;
    }

    bool HasValidNdviValues(const std::vector<MergedAllValInfosType> &values, double thrVal = NOT_AVAILABLE, bool filterVegWeeks = false) {
        bool hasValidNdvi = false;
        for(size_t i = 0; i<values.size(); i++) {
            if (filterVegWeeks && values[i].vegWeeks != true) {
                continue;
            }
            if (IsNA(values[i].ndviMeanVal)) {
                continue;
            }
            if (IsGreater(values[i].ndviMeanVal, thrVal)) {
                hasValidNdvi = true;
                break;
            }
        }
        return hasValidNdvi;
    }

    void DisplayFeature(const FeatureDescription& feature)
    {
        std::cout << feature.GetFieldId() << " ; " <<
                     feature.GetCountryCode() << " ; " <<
                     feature.GetYear() << " ; " <<
                     feature.GetMainCrop() << " ; " <<
                     feature.GetVegetationStart() << " ; " <<
                     feature.GetHarvestStart() << " ; " <<
                     feature.GetHarvestEnd() << " ; " <<
                     feature.GetPractice() << " ; " <<
                     feature.GetPracticeType() << " ; " <<
                     feature.GetPracticeStart() << " ; " <<
                     feature.GetPracticeEnd() << std::endl;
    }

    bool ComputeMeanAndStandardDeviation (const std::vector<double> &inVect,
                                          double &meanVal, double &stdDevVal,
                                          bool genSampleStandardDev = false) {
        if (inVect.size() == 0) {
            return false;
        }
        if (genSampleStandardDev && inVect.size() == 1) {
            return false;
        }
        double sum = std::accumulate(inVect.begin(), inVect.end(), 0.0);
        meanVal = sum / inVect.size();

        // Boost implementation
//        boost::accumulators::accumulator_set<double, boost::accumulators::stats<boost::accumulators::tag::variance> > acc;
//        for (int i = 0; i<inVect.size(); i++) {
//            acc(inVect[i]);
//        }
//        //cout << mean(acc) << endl;
//        stdDevVal = sqrt(boost::accumulators::variance(acc));

        // Non boost implementation

        std::vector<double> diff(inVect.size());
        std::transform(inVect.begin(), inVect.end(), diff.begin(), [meanVal](double x) { return x - meanVal; });
        double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
        int vecSize = inVect.size();
        if (genSampleStandardDev) {
            vecSize -= 1;
        }
        stdDevVal = std::sqrt(sq_sum / vecSize);

        return true;
    }

    void UpdateGapsInformation(time_t startTime, time_t endTime,
                               time_t ttPrevDate, time_t ttCurDate, int &sumVal, bool countAll = false) {
        // Check for gaps between harvest start and harvest end interval
        if (ttCurDate <= startTime || ttPrevDate >= endTime) {
            return;
        }
        if (startTime != 0 && endTime != 0) {
            if (ttPrevDate < startTime) {
                ttPrevDate = startTime;
            }
            if (ttPrevDate < endTime && ttCurDate >= endTime) {
                ttCurDate = endTime;
            }
            int diffInDays = (ttCurDate - ttPrevDate) / SEC_IN_DAY;
            if (diffInDays > 7) {
                sumVal += ((diffInDays / 7) - (countAll ? 0 : 1));
            }
        }
    }

    void UpdateGapsInformation(const std::vector<MergedAllValInfosType> &values, FieldInfoType &fieldInfos, const HarvestEvaluationInfoType &harvestEvalInfos) {
        int sum = 0;
        int sumHGaps = 0;
        int sumPGaps = 0;
        int diffInDays;
        bool bComputeHWS1GapsVal = false;
        int sumHWS1Gaps = 0;
        time_t ttPractConfWeekOffset = 0;

        const std::string & prevHWS1GapsVal = m_prevPrdReader.GetHWS1GapsInfosForFieldId(std::atoi(fieldInfos.fieldSeqId.c_str()));
        int nPrevHWS1GapsVal = std::atoi(prevHWS1GapsVal.c_str());
        if (prevHWS1GapsVal.length() > 0 && nPrevHWS1GapsVal > 0) {
            fieldInfos.h_W_S1GapsInfos = nPrevHWS1GapsVal;
        } else {
            if (!IsNA(harvestEvalInfos.harvestConfirmWeek)) {
                bComputeHWS1GapsVal = true;
                ttPractConfWeekOffset = harvestEvalInfos.ttHarvestConfirmWeekStart - SEC_IN_5_WEEKS;
            }
        }


        // according to ISO calendar, the first week of the year is the one that contains 4th of January
        time_t ttFirstWeekStart = GetTimeFromString(std::to_string(fieldInfos.year).append("-01-01"));
        time_t ttPrevDate = ttFirstWeekStart;
        time_t ttCurDate;
        if (values.size() > 0) {
            for(size_t i = 0; i < (values.size() + 1); i++) {
                if (i > 0) {
                    ttPrevDate = values[i-1].ttDate;
                }
                if (i<values.size()) {
                    ttCurDate = values[i].ttDate;
                } else {
                    ttCurDate = m_ttLimitAcqDate;
                }
                diffInDays = (ttCurDate - ttPrevDate) / SEC_IN_DAY;
                if (diffInDays > 7) {
                    sum += (diffInDays / 7) - 1;
                }

                // Check for gaps between harvest start and harvest end interval
                UpdateGapsInformation(fieldInfos.ttHarvestStartWeekFloorTime, fieldInfos.ttHarvestEndWeekFloorTime,
                                      ttPrevDate, ttCurDate, sumHGaps);
                // Check for gaps between practice start and harvest end interval
                UpdateGapsInformation(fieldInfos.ttPracticeStartWeekFloorTime, fieldInfos.ttPracticeEndWeekFloorTime,
                                      ttPrevDate, ttCurDate, sumPGaps);

                // Check for gaps in the preceding 5 weeks before the harvestConfirmWeek
                if (bComputeHWS1GapsVal) {
                    UpdateGapsInformation(ttPractConfWeekOffset, harvestEvalInfos.ttHarvestConfirmWeekStart,
                                          ttPrevDate, ttCurDate, sumHWS1Gaps, true);
                }
            }
        }
        fieldInfos.gapsInfos = sum;
        fieldInfos.hS1GapsInfos = sumHGaps;
        fieldInfos.pS1GapsInfos = sumPGaps;
        if (bComputeHWS1GapsVal) {
            fieldInfos.h_W_S1GapsInfos = sumHWS1Gaps;
        }
    }

    double ComputeOpticalThresholdBuffer(int nVegWeeksCnt, double maxMeanNdviDropVal, double OpticalThresholdValue) {
        double OpticalThresholdBuffer = -1;
        if (nVegWeeksCnt > 1) {
            OpticalThresholdBuffer = std::round((maxMeanNdviDropVal - OpticalThresholdValue) / m_OpticalThrBufDenominator);
        }
        if (OpticalThresholdBuffer < 0) {
            OpticalThresholdBuffer = 0;
        }
        return OpticalThresholdBuffer;
    }

    double ComputeAmplitudeThresholdValue(double meanVal, double stdDevVal) {
        //        grd.thr.value <- paste("grd.thr.value <- mean(grd.filtered)-sd(grd.filtered)/2")
        // or
        //        grd.thr.value <- paste("grd.thr.value <- mean(grd.filtered)
        return meanVal - (m_UseStdDevInAmpThrValComp ? (stdDevVal / m_AmpThrValDenominator) : 0);
    }

    double ComputeAmplitudeThresholdBreak(double stdDevVal) {
        double ampThrBreak = stdDevVal / m_AmpThrBreakDenominator;
        // minimum change
        if (IsLess(ampThrBreak, m_AmpThrMinimum)) {
            ampThrBreak = m_AmpThrMinimum;
        }
        return ampThrBreak;
    }

private:
    std::unique_ptr<StatisticsInfosReaderBase> m_pAmpReader;
    std::unique_ptr<StatisticsInfosReaderBase> m_pNdviReader;
    std::unique_ptr<StatisticsInfosReaderBase> m_pCoheReader;

    std::unique_ptr<PracticeReaderBase> m_pPracticeReader;

    std::string m_outputDir;
    std::string m_countryName;
    std::string m_practiceName;
    std::string m_year;

    double m_OpticalThrVegCycle;
    // for MARKER 2 - NDVI loss
    // expected value of harvest/clearance
    double m_NdviDown;
    // buffer value (helps in case of sparse ndvi time-series)
    double m_NdviUp;
    // opt.thr.value is round up to ndvi.step
    double m_ndviStep;
    double m_OpticalThresholdMinimum;

    // for MARKER 5 - COHERENCE increase
    double m_CohThrBase;
    double m_CohThrHigh;
    double m_CohThrAbs;

    // for MARKER 3 - BACKSCATTER loss
    double m_AmpThrMinimum;

    // INPUT THRESHOLDS - EFA PRACTICE evaluation
    std::string m_CatchMain;
    std::string m_CatchCropIsMain;
    // TODO: Ask Gisat for CZE
    int m_CatchPeriod;               // in days (e.g. 8 weeks == 56 days)
    double m_CatchProportion;       // buffer threshold
    std::string m_CatchPeriodStart;

    int m_EfaNdviThr;
    int m_EfaNdviUp;
    int m_EfaNdviDown;

    double m_EfaCohChange;
    double m_EfaCohValue;

    double m_EfaNdviMin;
    double m_EfaAmpThr;

    bool m_UseStdDevInAmpThrValComp;
    int m_OpticalThrBufDenominator;
    int m_AmpThrBreakDenominator;
    int m_AmpThrValDenominator;

    std::string m_flMarkersStartDateStr;
    std::string m_flMarkersEndDateStr;

    std::string m_CatchCropVal;
    std::string m_FallowLandVal;
    std::string m_NitrogenFixingCropVal;

    // # optional: in case graphs shall be generated set the value to TRUE otherwise set to FALSE
    TsaPlotsWriter m_plotsWriter;
    TsaCSVWriter m_csvWriter;
    TsaContinuousFileWriter m_contFileWriter;
    TsaDebugPrinter m_debugPrinter;

    TsaPrevPrdReader m_prevPrdReader;

    bool m_bAllowGaps;
    bool m_bGapsFill;

    bool m_bDebugMode;
    bool m_bVerbose;

    int m_nMinS1PixCnt;

    time_t m_ttLimitAcqDate;

};

} // end of namespace Wrapper
} // end of namespace otb

OTB_APPLICATION_EXPORT(otb::Wrapper::TimeSeriesAnalysis)
