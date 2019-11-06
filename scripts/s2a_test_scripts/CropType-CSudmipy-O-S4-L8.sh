#!/bin/bash

source set_build_folder.sh

./CropType.py \
    -ref /mnt/Sen2Agri_DataSets/In-Situ_TDS/France/LC/SudmipyS2A_LandCoverDecoupe_dissolvedGeometry.shp \
    -input \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130216_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130216_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130221_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130221_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130303_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130303_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130308_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130308_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130318_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130318_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130323_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130323_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130407_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130407_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130412_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130412_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130417_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130417_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130422_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130422_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130512_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130512_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130517_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130517_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130527_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130527_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130606_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130606_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130611_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130611_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Sudmipy-West/SPOT4_HRVIR_XS_20130616_N2A_CSudmipy-OD0000B0000/SPOT4_HRVIR_XS_20130616_N2A_CSudmipy-OD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20130414_L8_199_030/SudouestS2A_20130414_L8_199_030.HDR \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20130719_L8_199_030/SudouestS2A_20130719_L8_199_030.HDR \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20130804_L8_199_030/SudouestS2A_20130804_L8_199_030.HDR \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20130820_L8_199_030/SudouestS2A_20130820_L8_199_030.HDR \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20130905_L8_199_030/SudouestS2A_20130905_L8_199_030.HDR \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20131007_L8_199_030/SudouestS2A_20131007_L8_199_030.HDR \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20131023_L8_199_030/SudouestS2A_20131023_L8_199_030.HDR \
    /mnt/Sen2Agri_DataSets/L2A/LANDSAT8/Sudmipy-West/MACCS_ManualFormat/SudouestS2A_20131210_L8_199_030/SudouestS2A_20131210_L8_199_030.HDR \
    -rseed 0 -pixsize 20 \
    -outdir /mnt/data/csudmipy/CSudmipy-O-type \
    -buildfolder $BUILD_FOLDER
