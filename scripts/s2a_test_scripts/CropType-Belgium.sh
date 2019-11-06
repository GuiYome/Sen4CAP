#!/bin/bash

source set_build_folder.sh

./CropType.py \
    -ref /mnt/Sen2Agri_DataSets/Belgium/TDS/InSitu/LC/BE_HESB_LC_II_2013.shp \
    -input \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130318_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130318_N2A_EBelgiumD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130402_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130402_N2A_EBelgiumD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130407_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130407_N2A_EBelgiumD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130417_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130417_N2A_EBelgiumD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130422_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130422_N2A_EBelgiumD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130507_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130507_N2A_EBelgiumD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130527_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130527_N2A_EBelgiumD0000B0000.xml \
    /mnt/Sen2Agri_DataSets/L2A/Spot4-T5/Belgium/SPOT4_HRVIR1_XS_20130606_N2A_EBelgiumD0000B0000/SPOT4_HRVIR1_XS_20130606_N2A_EBelgiumD0000B0000.xml \
    -t0 20130318 -tend 20130606 -rate 5 \
    -rseed 0 -pixsize 20 \
    -mask /mnt/output/L4A/SPOT4-T5/Belgium/work/crop_mask.tif \
    -outdir /mnt/output/L4B/SPOT4-T5/Belgium/work \
    -buildfolder $BUILD_FOLDER
