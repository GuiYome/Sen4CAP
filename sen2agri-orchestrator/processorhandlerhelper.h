#ifndef PROCESSORHANDLERHELPER_H
#define PROCESSORHANDLERHELPER_H

#include "model.hpp"

// 23*3600+59*60+59
#define SECONDS_IN_DAY 86400
class ProcessorHandlerHelper
{
public:
    typedef enum {L2_PRODUCT_TYPE_UNKNOWN = 0,
                  L2_PRODUCT_TYPE_S2 = 1,
                  L2_PRODUCT_TYPE_L8 = 2,
                  L2_PRODUCT_TYPE_SPOT4 =3,
                  L2_PRODUCT_TYPE_SPOT5 = 4} L2ProductType;

    typedef enum {SATELLITE_ID_TYPE_UNKNOWN = 0,
                  SATELLITE_ID_TYPE_S2 = 1,
                  SATELLITE_ID_TYPE_L8 = 2,
                  SATELLITE_ID_TYPE_SPOT4 =3,
                  SATELLITE_ID_TYPE_SPOT5 = 4} SatelliteIdType;

    typedef struct {
        L2ProductType productType;
        SatelliteIdType satelliteIdType;
        // position of the date in the name when split by _
        int dateIdxInName;
        // The expected extension of the tile metadata file
        QString extension;
        //Regext for the product name
        QString tileNameRegex;
        // TODO: Not used for now
        int tileIdxInName;
    } L2MetaTileNameInfos;

    typedef struct InfoTileFile {
        QString file;
        SatelliteIdType satId;
        QString acquisitionDate;
        QStringList additionalFiles;
        inline bool operator==(const InfoTileFile& rhs) {
            if(satId != rhs.satId) {
                return false;
            }
            if(acquisitionDate != rhs.acquisitionDate) {
                return false;
            }
            if(file != rhs.file) {
                return false;
            }
            if(additionalFiles.size() > 0 && rhs.additionalFiles.size() > 0 && additionalFiles[0] != rhs.additionalFiles[0]) {
                return false;
            }
            return true;
        }
    } InfoTileFile;

    typedef struct {
        QString tileId;
        QList<InfoTileFile> temporalTilesFileInfos;
        //the unique sattelites ids from the above list
        QList<SatelliteIdType> uniqueSatteliteIds;
        SatelliteIdType primarySatelliteId;
        QString shapePath;
        QString projectionPath;
        QMap<SatelliteIdType, TileList> satIntersectingTiles;
    } TileTemporalFilesInfo;


    ProcessorHandlerHelper();

    static ProductType GetProductTypeFromFileName(const QString &path, bool useParentDirIfDir = true);
    static QString GetTileId(const QString &path, SatelliteIdType &satelliteId);
    static QStringList GetTileIdsFromHighLevelProduct(const QString &prodFolder);
    static QMap<QString, TileTemporalFilesInfo> GroupTiles(const QStringList &listAllProductsTiles, QList<SatelliteIdType> &outAllSatIds, SatelliteIdType &outPrimarySatelliteId);
    static QMap<QDate, QStringList> GroupL2AProductTilesByDate(const QMap<QString, QStringList> &inputProductToTilesMap);
    static SatelliteIdType GetPrimarySatelliteId(const QList<ProcessorHandlerHelper::SatelliteIdType> &satIds);
    static QString GetMissionNamePrefixFromSatelliteId(SatelliteIdType satId);
    static QStringList GetTextFileLines(const QString &filePath);
    static QString GetFileNameFromPath(const QString &filePath);
    static bool IsValidHighLevelProduct(const QString &path);
    static bool GetHigLevelProductAcqDatesFromName(const QString &productName, QDateTime &minDate, QDateTime &maxDate);
    static QString GetHigLevelProductTileFile(const QString &tileDir, const QString &fileIdentif, bool isQiData=false);
    static QMap<QString, QString> GetHigLevelProductFiles(const QString &productDir, const QString &fileIdentif, bool isQiData=false);
    static QMap<QString, QString> GetHighLevelProductTilesDirs(const QString &productDir);
    static QString GetHighLevelProductIppFile(const QString &productDir);
    static QString GetSourceL2AFromHighLevelProductIppFile(const QString &productDir, const QString &tileFilter = "");
    static bool HighLevelPrdHasL2aSource(const QString &highLevelPrd, const QString &l2aPrd);
    static QMap<QString, QStringList> GroupHighLevelProductTiles(const QStringList &listAllProductFolders);

    //static QString GetL2ATileMainImageFilePath(const QString &tileMetadataPath);
    static const L2MetaTileNameInfos &GetL2AProductTileNameInfos(const QString &metaFileName);
    static SatelliteIdType GetL2ASatelliteFromTile(const QString &tileMetadataPath);
    static L2ProductType GetL2AProductTypeFromTile(const QString &tileMetadataPath);
    static QDateTime GetL2AProductDateFromPath(const QString &path);
    static bool IsValidL2AMetadataFileName(const QString &path);
    static bool GetL2AIntevalFromProducts(const QStringList &productsList, QDateTime &minTime, QDateTime &maxTime);
    static bool GetCropReferenceFile(const QString &refDir, QString &shapeFile, QString &referenceRasterFile);
    static bool GetStrataFile(const QString &refDir, QString &strataShapeFile);
    static bool GetCropReferenceFile(const QString &refDir, QString &shapeFile, QString &referenceRasterFile, QString &strataShapeFile);
    static void AddSatteliteIntersectingProducts(QMap<QString, TileTemporalFilesInfo> &mapSatellitesTilesInfos,
                                                 QStringList &listSecondarySatLoadedProds, SatelliteIdType secondarySatId,
                                                 TileTemporalFilesInfo &primarySatInfos);
    static QString BuildShapeName(const QString &shapeFilesDir, const QString &tileId, int jobId, int taskId);
    static QString BuildProjectionFileName(const QString &projFilesDir, const QString &tileId, int jobId, int taskId);
    static QString GetShapeForTile(const QString &shapeFilesDir, const QString &tileId);
    static QString GetProjectionForTile(const QString &projFilesDir, const QString &tileId);
    static QStringList GetTemporalTileFiles(const TileTemporalFilesInfo &temporalTileInfo);
    static QStringList GetTemporalTileAcquisitionDates(const TileTemporalFilesInfo &temporalTileInfo);
    static bool TemporalTileInfosHasFile(const TileTemporalFilesInfo &temporalTileInfo, const QString &filePath);
    static void SortTemporalTileInfoFiles(TileTemporalFilesInfo &temporalTileInfo);
    static void TrimLeftSecondarySatellite(QStringList &productsList);
    static void TrimLeftSecondarySatellite(QStringList &productsList, QMap<QString, TileTemporalFilesInfo> mapTiles);
    static SatelliteIdType ConvertSatelliteType(Satellite satId);

    static QDateTime GetNdviProductTime(const QString &prdPath);
    static QDateTime GetS1L2AProductTime(const QString &prdPath);
    static void UpdateMinMaxTimes(const QDateTime &newTime, QDateTime &minTime, QDateTime &maxTime);


private:
    static QMap<QString, L2MetaTileNameInfos> m_mapSensorL2ATileMetaFileInfos;
    static QMap<QString, ProductType> m_mapHighLevelProductTypeInfos;
};

#endif // PROCESSORHANDLERHELPER_H
