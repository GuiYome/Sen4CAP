<?php
function getSubSteps($processorId, $stepAreSubPrds) {
    $db = pg_connect ( ConfigParams::getConnection() ) or die ( "Could not connect" );
    $result = pg_query($db, "SELECT id, short_name FROM sp_get_processors() WHERE name NOT LIKE '%NDVI%'") or die ("Could not execute.");
    if (pg_num_rows($result) > 0) {
        while ( $row = pg_fetch_row ( $result ) ) {
            $id = $row[0];
            if ($id == $processorId) {
                $short = strtolower($row[1]); 
                $subkey = ".sub_steps";
                if ($stepAreSubPrds === true) {
                    $subkey = ".sub_products";
                }
                $key = "processor." . $short . $subkey;
                $sql = "SELECT site_id,value FROM sp_get_parameters('$key')";
                $result2 = pg_query ( $db, $sql ) or die ( "Could not execute." );
                while ( $row2 = pg_fetch_row ( $result2 ) ) {
                    $site_id = $row2 [0];
                    $value = $row2 [1];
                    if (is_null($site_id)) {
                        if (!is_null($value)) {
                            $values = explode(',', $value);
                            if (isset($values) && is_array($values) && !empty($values))
                                return $values;                            
                        }
                        break;
                    }
                }
                break;
            }
        }
    }
    return array();
}

function getAction($processorId) {
	$action = "dashboard.php";
	return $action;
}
function get_scheduled_jobs_header($processorId) {
?>
	<div class="schedule-header">
		<span>Job name</span>
		<span>Site name</span>
		<span>Season name</span>
		<span>Schedule type</span>
		<span>First run time</span>
		<span style="min-width:150px">Repeat</span>
		<span style="min-width:110px">Action</span>
	</div>
<?php
}
function add_new_scheduled_jobs_layout($processorId) {
	$action = getAction($processorId);

	$db = pg_connect ( ConfigParams::getConnection() ) or die ( "Could not connect" );
	// get distinct sites with seasons
	
	if($_SESSION['isAdmin'] ||  sizeof($_SESSION['siteId'])>0){
	    $sites =  sizeof($_SESSION['siteId'])==0? "":" AND st.id in (".implode(",",$_SESSION['siteId']).") ";
	    
    	$sql = "SELECT DISTINCT st.id, st.name FROM sp_get_sites(null) st, sp_get_site_seasons(null) ss WHERE st.id = ss.site_id ".$sites." ORDER BY st.name";
    	$result = pg_query ( $db, $sql ) or die ( "Could not execute." );
    	$option_site = "";
    	while ( $row = pg_fetch_row ( $result ) ) {
    		$option = "<option value='" . $row [0] . "'>" . $row [1] . "</option>";
    		$option_site = $option_site . $option;
    	}
	}
	// get all seasons with associated sites
	$sql = "SELECT id, site_id, name FROM sp_get_site_seasons(null) ORDER BY site_id, name";
	$result = pg_query ( $db, $sql ) or die ( "Could not execute." );
	$option_season = "";
	while ( $row = pg_fetch_row ( $result ) ) {
		$option = "<option class='hidden' value='" . $row [0] . "' data-siteid='" . $row [1] . "'>" . $row [2] . "</option>";
		$option_season = $option_season . $option;
	}
	?>
	<form class="schedule-row shedule-row-add" role="form" name="jobform" id="jobform" method="post" action="<?= $action ?>" style="padding:10px;">
		<input type="hidden" value="<?= $processorId ?>" name="processorId">
		<span class="schedule_format">
			<input type="text" name="jobname" id="jobname" class="schedule_format">
		</span>
		<span class="schedule_format">
			<select id="sitename<?= $processorId ?>" name="sitename" onchange="selectedSiteAdd(<?= $processorId ?>)">
				<option value="" selected>Select site</option>
				<?= $option_site ?>
			</select>
		</span>
		<span class="schedule_format">
			<select id="seasonname<?= $processorId ?>" name="seasonname">
				<option value="" selected>Select season</option>
				<?= $option_season ?>
			</select>
		</span>
        <?php 
        $prdOrStep = "product";
        $subPrds = getSubSteps($processorId, true);
        if (empty($subPrds)) {
            $subPrds = getSubSteps($processorId, false);
            if (!empty($subPrds)) {
                $prdOrStep = "step";
            }
        } 
        if (!empty($subPrds)) { ?>
            <span class="schedule_format">        
                <select id="product_add<?= $processorId ?>" name="product_add" required="true">
                    <option value="" selected>Select a <?= $prdOrStep ?></option>
            <?php foreach ($subPrds as $step) { ?>
                    <option value="<?= $step ?>"><?= $step ?></option>
           <?php } ?>
                </select>
            </span>
        <?php } ?>
		<span class="schedule_format">
			<select id="schedule_add<?= $processorId ?>" name="schedule_add" onchange="selectedScheduleAdd(event, <?= $processorId ?>)">
				<option value="" selected>Select a schedule</option>
				<option value="0">Once</option>
				<option value="1">Cycle</option>
				<option value="2">Repeat</option>
			</select>
		</span>
		<span class="schedule_format div_startdatefoo">
			-
		</span>
		<span class="schedule_format div_startdate hidden">
			<input type="text" name="startdate" id="add_startdate" class="schedule_format startdate">
		</span>
		<span class="schedule_format div_repeatfoo">
			-
		</span>
		<span class="schedule_format div_repeatnever hidden">
			Never
		</span>
		<span class="schedule_format div_repeatafter hidden">
			After &nbsp;&nbsp;<input type="number" class="schedule_format" name="repeatafter" id="repeatafter"  value="" min="1" step="1"> days
		</span>
		<span class="schedule_format div_oneverydate hidden">
			Every <input type="number" class="schedule_format" name="oneverydate" id="oneverydate" min="1" max="31" step="1" value=""> day of month
		</span>
		<span class="schedule_format">
			<!--  <input type="submit" class="button add-edit-btn" name="schedule_saveJob" value="Save">-->
			
			<div style="display: table-row">
				<span class="schedule_format">
				<input type="submit" class="button btn-success btn-xs" name="schedule_saveJob" value="Save">
				<input type="submit" class="button btn-danger btn-xs"  name="schedule_submit_delete" value="Delete" style="visibility: hidden">
				</span>
			</div>
		</span>		
	</form>
<?php
	$_SESSION['proc_id'] = $processorId;
}
function update_scheduled_jobs_layout($processorId) {
	$action = getAction($processorId);

	$db = pg_connect ( ConfigParams::getConnection() ) or die ( "Could not connect" );
	$sql = "SELECT * from sp_get_dashboard_processor_scheduled_task('$processorId')";
	$result = pg_query ( $db, $sql ) or die ( "Could not execute." );

	$scheduled_task = array();
	while ( $row = pg_fetch_row ( $result ) ) {
	    if($_SESSION['isAdmin'] == false){
	        if(sizeof($_SESSION['siteId'])>0 && in_array($row[2],$_SESSION['siteId'])){
	            $scheduled_task[] = $row;
	        }
	    }else{
	        $scheduled_task[] = $row;
	    }
	}
	
	if(sizeof($scheduled_task)>0){
	    foreach ($scheduled_task as $task){
	        $jobId           = $task[0];
	        $jobName         = $task[1];
	        $siteName        = $task[3];
	        $seasonName      = $task[4];
	        $repeatType      = $task[5];
	        $firstRunTime    = $task[6];
	        $repeatAfterDays = $task[7];
	        $repeatOnEvery   = $task[8];
	    
	    
		?>
		<form class="schedule-row" role="form" name="jobform_edit" method="post" action="<?= $action ?>">
			<input type="hidden" value="<?= $jobId ?>" name="scheduledID">
			<input type="hidden" value="<?= $processorId ?>" name="processorId">
			<span class="schedule_format"><?= $jobName ?></span>
			<span class="schedule_format"><?= $siteName ?></span>
			<span class="schedule_format"><?= $seasonName ?></span>
			<span class="schedule_format">
				<select id="schedule<?= $jobId ?>" name="schedule" onchange="selectedSchedule(event, <?= $jobId ?>)">
					<option value="0"<?= $repeatType == 0 ? " selected" : "" ?>>Once</option>
					<option value="1"<?= $repeatType == 1 ? " selected" : "" ?>>Cycle</option>
					<option value="2"<?= $repeatType == 2 ? " selected" : "" ?>>Repeat</option>
				</select>
			</span>
			<span class="schedule_format div_startdate">
				<input type="text" class="startdate schedule_format" name="startdate" id="startdate<?= $jobId ?>" value="<?= $firstRunTime ?>" onChange="activateButton(<?= $jobId ?>)" >
			</span>
			<span class="schedule_format<?= ($repeatType != 0) ? " hidden" : "" ?> div_repeatnever">
				Never
			</span>
			<span class="schedule_format<?= ($repeatType != 1) ? " hidden" : "" ?> div_repeatafter">
				After &nbsp;&nbsp;<input type="number" class="schedule_format" name="repeatafter" id="repeatafter<?= $jobId ?>" value="<?= $repeatAfterDays ?>" onChange="checkMin(<?= $jobId ?>)" min="0"> days
			</span>
			<span class="schedule_format<?= ($repeatType != 2) ? " hidden" : "" ?> div_oneverydate">
				Every <input type="number" class="schedule_format" name="oneverydate" id="oneverydate<?= $jobId ?>" value="<?= $repeatOnEvery ?>" onChange="setMin(<?= $jobId ?>)" min="0" max="31" step="1"> day of month
			</span>
			<span class="schedule_format">
				<div style="display: table-row">
					<span class="schedule_format" style="border-top: none;">
					<!--  <input type="submit" class="button add-edit-btn" name="schedule_submit" id="schedule_submit<?= $jobId ?>" value="Save" disabled>-->
					<input type="submit" class="button btn-success btn-xs" name="schedule_submit" id="schedule_submit<?= $jobId ?>" value="Save" disabled>
					<input type="submit" class="button btn-danger btn-xs"  name="schedule_submit_delete" id="schedule_submit_delete<?= $jobId ?>" value="Delete">
					</span>
				</div>
			</span>
		</form>
		<?php
		}
	}
}
?>
