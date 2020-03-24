﻿CREATE OR REPLACE FUNCTION reports.sp_reports_l8_detail(
	IN siteId smallint DEFAULT NULL::smallint,
	IN orbitId integer DEFAULT NULL::integer,
	IN fromDate date DEFAULT NULL::date,
	IN toDate date DEFAULT NULL::date)
  RETURNS TABLE(site_id smallint, downloader_history_id integer, orbit_id integer, acquisition_date date, product_name character varying,
		status_description character varying, status_reason character varying, l2_product character varying, clouds integer) AS
$BODY$
DECLARE startDate date;
DECLARE endDate date;
BEGIN
	IF $3 IS NULL THEN
		SELECT MIN(acquisition_date) INTO startDate FROM reports.l8_report;
	ELSE
		SELECT fromDate INTO startDate;
	END IF;
	IF $4 IS NULL THEN
		SELECT MAX(acquisition_date) INTO endDate FROM reports.l8_report;
	ELSE
		SELECT toDate INTO endDate;
	END IF;
	RETURN QUERY
	SELECT r.site_id, r.downloader_history_id, r.orbit_id::integer, r.acquisition_date, r.product_name, r.status_description, r.status_reason, r.l2_product, r.clouds 
		FROM reports.l8_report r
		WHERE ($1 IS NULL OR r.site_id = $1) AND ($2 IS NULL OR r.orbit_id::integer = $2) AND r.acquisition_date BETWEEN startDate AND endDate
	ORDER BY r.site_id, r.acquisition_date, r.product_name;
END

$BODY$
  LANGUAGE plpgsql STABLE
  COST 100
  ROWS 1000;
ALTER FUNCTION reports.sp_reports_l8_detail(smallint, integer, date, date)
  OWNER TO postgres;