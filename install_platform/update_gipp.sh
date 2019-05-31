#!/bin/bash

function install_gipp_files() 
{
    L2A_PROCESSOR_PATH=$(sudo -u postgres psql sen2agri -qtA -c "select value from config where key='demmaccs.maccs-launcher';")
    GIPP_PATH=$(sudo -u postgres psql sen2agri -qtA -c "select value from config where key='demmaccs.gips-path';")
    GIPP_PATH=${GIPP_PATH%/}
    l1c_processor=0
    if ! [[ -z "${GIPP_PATH}" ]] ; then
        echo "GIPP files found configured in ${GIPP_PATH}. Replacing ..."
        DATE=$(date +"%Y_%m_%dT%H_%M_%S")
        echo "Backing up ${GIPP_PATH} to ${GIPP_PATH}_OLD_${DATE}"
        mv "${GIPP_PATH}" "${GIPP_PATH}_OLD_${DATE}"
        mkdir -p "${GIPP_PATH}"
        echo "Copying new GIPP files to ${GIPP_PATH} ..."
	while [[ $answer != '1' ]] && [[ $answer != '2' ]]
	do	
	    read -n1 -p "The new GIPP files should be copied for MACCS or for MAJA? (1 for MACCS / 2 for MAJA): " -r answer
	    printf "\n"
	    case $answer in
		1)
            if [[ $L2A_PROCESSOR_PATH != *"maccs"* ]]; then
                echo "MACCS GIPP were selected to be set but MACCS is not set in demmaccs.maccs-launcher from config table!"
                echo "Please update first this path to be MACCS (ex. /opt/maccs/core/5.1/bin/maccs)!"
            else
                echo "MACCS GIPP files will be copied"
                l1c_processor=1
            fi
		    ;;
		2)
            if [[ $L2A_PROCESSOR_PATH != *"maja"* ]]; then
                echo "MAJA GIPP were selected to be set but MAJA is not set in demmaccs.maccs-launcher from config table!"
                echo "Please update first this path to be MAJA (ex. /opt/maja/3.2.2/bin/maja)!"
            else
                echo "MAJA GIPP files will be copied"
                l1c_processor=2
            fi
		    ;;
		*)
		    echo "Unknown answer"
		    ;;
	    esac
	done
	if [ $l1c_processor -eq 1 ]; then	
            cp -fR ../gipp/* "${GIPP_PATH}"
	elif [ $l1c_processor -eq 2 ]; then
	    cp -fR ../gipp_maja/* "${GIPP_PATH}"
	fi	
	    
        echo "Done!"
    fi
}

systemctl stop sen2agri-demmaccs.timer sen2agri-demmaccs

install_gipp_files

systemctl start sen2agri-demmaccs.timer


