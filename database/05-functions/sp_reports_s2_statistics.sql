﻿CREATE OR REPLACE FUNCTION reports.sp_reports_s2_statistics(
	IN siteId smallint DEFAULT NULL::smallint,
	IN orbitId integer DEFAULT NULL::integer,
	IN fromDate date DEFAULT NULL::date,
	IN toDate date DEFAULT NULL::date)
  RETURNS TABLE(calendar_date date, acquisitions integer, failed_to_download integer, processed integer, not_yet_processed integer, falsely_processed integer, errors integer, clouds integer) AS
$BODY$
DECLARE startDate date;
DECLARE endDate date;
BEGIN
	IF $3 IS NULL THEN
		SELECT MIN(acquisition_date) INTO startDate FROM reports.s2_report;
	ELSE
		SELECT fromDate INTO startDate;
	END IF;
	IF $4 IS NULL THEN
		SELECT MAX(acquisition_date) INTO endDate FROM reports.s2_report;
	ELSE
		SELECT toDate INTO endDate;
	END IF;
	RETURN QUERY
	WITH 	calendar AS 
			(SELECT date_trunc('day', dd)::date AS cdate 
				FROM generate_series(startDate::timestamp, endDate::timestamp, '1 day'::interval) dd),
		ac AS 
			(SELECT acquisition_date, COUNT(DISTINCT downloader_history_id) AS acquisitions 
				FROM reports.s2_report 
				WHERE ($1 IS NULL OR site_id = $1) AND ($2 IS NULL OR orbit_id = $2) AND acquisition_date BETWEEN startDate AND endDate
				GROUP BY acquisition_date 
				ORDER BY acquisition_date),
		proc AS 
			(SELECT acquisition_date, COUNT(DISTINCT downloader_history_id) AS cnt 
				FROM reports.s2_report 
				WHERE status_description = 'processed' AND l2_product IS NOT NULL AND
					($1 IS NULL OR site_id = $1) AND ($2 IS NULL OR orbit_id = $2) AND acquisition_date BETWEEN startDate AND endDate
				GROUP BY acquisition_date 
				ORDER BY acquisition_date),
		ndld AS 
			(SELECT acquisition_date, count(downloader_history_id) AS cnt 
				FROM reports.s2_report 
				WHERE status_description IN ('failed','aborted') AND l2_product IS NULL AND
					($1 IS NULL OR site_id = $1) AND ($2 IS NULL OR orbit_id = $2) AND acquisition_date BETWEEN startDate AND endDate
				GROUP BY acquisition_date 
				ORDER BY acquisition_date),
		dld AS 
			(SELECT acquisition_date, COUNT(downloader_history_id) AS cnt 
				FROM reports.s2_report 
				WHERE status_description IN ('downloaded', 'processing') AND
					($1 IS NULL OR site_id = $1) AND ($2 IS NULL OR orbit_id = $2) AND acquisition_date BETWEEN startDate AND endDate
				GROUP BY acquisition_date 
				ORDER BY acquisition_date),
		fproc AS 
			(SELECT acquisition_date, COUNT(DISTINCT downloader_history_id) AS cnt 
				FROM reports.s2_report 
				WHERE status_description = 'processed' AND l2_product IS NULL AND
					($1 IS NULL OR site_id = $1) AND ($2 IS NULL OR orbit_id = $2) AND acquisition_date BETWEEN startDate AND endDate
				GROUP BY acquisition_date 
				ORDER BY acquisition_date),
		e AS 
			(SELECT acquisition_date, COUNT(DISTINCT downloader_history_id) AS cnt 
				FROM reports.s2_report 
				WHERE status_description LIKE 'processing_%failed' AND
					($1 IS NULL OR site_id = $1) AND ($2 IS NULL OR orbit_id = $2) AND acquisition_date BETWEEN startDate AND endDate
				GROUP BY acquisition_date 
				ORDER BY acquisition_date),
		cld AS 
			(SELECT acquisition_date, COUNT(DISTINCT downloader_history_id) AS cnt 
				FROM reports.s2_report 
				WHERE s2_report.clouds > 90 AND
					($1 IS NULL OR site_id = $1) AND ($2 IS NULL OR orbit_id = $2) AND acquisition_date BETWEEN startDate AND endDate
				GROUP BY acquisition_date 
				ORDER BY acquisition_date)
	SELECT 	c.cdate, 
		COALESCE(ac.acquisitions, 0)::integer,
		COALESCE(ndld.cnt, 0)::integer,
		COALESCE(proc.cnt, 0)::integer, 
		COALESCE(dld.cnt, 0)::integer,
		COALESCE(fproc.cnt, 0)::integer,
		COALESCE(e.cnt, 0)::integer,
		COALESCE(cld.cnt, 0)::integer
	FROM calendar c
		LEFT JOIN ac ON ac.acquisition_date = c.cdate
		LEFT JOIN ndld ON ndld.acquisition_date = c.cdate
		LEFT JOIN proc ON proc.acquisition_date = c.cdate
		LEFT JOIN dld ON dld.acquisition_date = c.cdate
		LEFT JOIN fproc ON fproc.acquisition_date = c.cdate
		LEFT JOIN e ON e.acquisition_date = c.cdate
		LEFT JOIN cld ON cld.acquisition_date = c.cdate;
END

$BODY$
  LANGUAGE plpgsql STABLE
  COST 100
  ROWS 1000;
ALTER FUNCTION reports.sp_reports_s2_statistics(smallint, integer, date, date)
  OWNER TO postgres;
