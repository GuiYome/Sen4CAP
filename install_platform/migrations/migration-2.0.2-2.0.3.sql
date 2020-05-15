begin transaction;

do $migration$
declare _statement text;
begin
    raise notice 'running migrations';

    if exists (select * from information_schema.tables where table_schema = 'public' and table_name = 'meta') then
        if exists (select * from meta where version in ('2.0.2', '2.0.3')) then
            if exists (select * from meta where version = '2.0.2') then
                raise notice 'upgrading from 2.0.2 to 2.0.3';

                raise notice 'patching 2.0.2';

                if exists (select * from config where key = 'executor.module.path.lai-end-of-job') then
                    _statement := $str$
                        UPDATE config SET value = '/usr/bin/true' WHERE key = 'executor.module.path.lai-end-of-job';
                    $str$;
                    raise notice '%', _statement;
                    execute _statement;
                end if;

                if not exists (select * from config where key = 'executor.module.path.lai-processor-end-of-job') then
                    _statement := $str$
                        INSERT INTO config(key, site_id, value, last_updated) VALUES ('executor.module.path.lai-processor-end-of-job', NULL, '/usr/bin/true', '2020-04-24 14:56:57.501918+02');
                    $str$;
                    raise notice '%', _statement;
                    execute _statement;
                end if;

                _statement := $str$                
                    CREATE OR REPLACE FUNCTION public.check_season()
                            RETURNS TRIGGER AS
                        $BODY$
                        BEGIN
                            IF NOT EXISTS (SELECT id FROM public.season WHERE id != NEW.id AND site_id = NEW.site_id AND enabled = true AND start_date <= NEW.start_date AND end_date >= NEW.end_date) THEN
                                RETURN NEW;
                            ELSE
                                RAISE EXCEPTION 'Nested seasons are not allowed';
                            END IF;
                        END;
                        $BODY$
                        LANGUAGE plpgsql VOLATILE;
                $str$;
                raise notice '%', _statement;
                execute _statement;   

                if not exists (select tgname from pg_trigger where not tgisinternal and tgrelid = 'season'::regclass and tgname = 'check_season_dates') then
                    _statement := $str$
                        CREATE TRIGGER check_season_dates 
                            BEFORE INSERT OR UPDATE ON public.season
                            FOR EACH ROW EXECUTE PROCEDURE public.check_season();
                    $str$;
                    raise notice '%', _statement;
                    execute _statement;
                end if;

                if not exists (select * from config where key = 'processor.s4c_l4a.mode') then
                    _statement := $str$
                        INSERT INTO config(key, site_id, value, last_updated) VALUES ('processor.s4c_l4a.mode', NULL, 'both', '2019-02-19 11:09:58.820032+02');
                        INSERT INTO config_metadata VALUES ('processor.s4c_l4a.mode', 'Mode', 'string', FALSE, 5, TRUE, 'Mode (both, s1-only, s2-only)', '{"min":"","step":"","max":""}');
                    $str$;
                    raise notice '%', _statement;
                    execute _statement;
                end if;

                _statement := $str$
                    DROP FUNCTION IF EXISTS sp_get_dashboard_products_nodes(integer[], integer[], smallint, integer[], timestamp with time zone, timestamp with time zone, character varying[], boolean);
                    
                    CREATE OR REPLACE FUNCTION sp_get_dashboard_products_nodes(
                        _user_name character varying,
                        _site_id integer[] DEFAULT NULL::integer[],
                        _product_type_id integer[] DEFAULT NULL::integer[],
                        _season_id smallint DEFAULT NULL::smallint,
                        _satellit_id integer[] DEFAULT NULL::integer[],
                        _since_timestamp timestamp with time zone DEFAULT NULL::timestamp with time zone,
                        _until_timestamp timestamp with time zone DEFAULT NULL::timestamp with time zone,
                        _tiles character varying[] DEFAULT NULL::character varying[],
                        _get_nodes boolean DEFAULT false)
                      RETURNS SETOF json AS
                    $BODY$
                        DECLARE q text;
                        BEGIN
                            q := $sql$
                            WITH
                            product_type_names(id, name, description, row, is_raster) AS (
                            select id, name, description, row_number() over (order by description), is_raster
                            from product_type
                            -- LPIS products should be excluded
                            where name != 'lpis'
                            ),
                        $sql$;

                        IF $9 IS TRUE THEN
                            q := q || $sql$
                            site_names(id, name, geog, row) AS (
                            select s.id, s.name, st_astext(s.geog), row_number() over (order by s.name)
                            from site s
                            join public.user u on u.login = $1 and (u.role_id = 1 or s.id in (select * from unnest(u.site_id)))
                            ),
                            data(id, product, footprint, site_coord, product_type_id, satellite_id, is_raster) AS (

                            SELECT
                            P.id,
                            P.name,
                            P.footprint,
                            S.geog,
                            PT.id,
                            P.satellite_id,
                            PT.is_raster
                            $sql$;
                            ELSE
                            q := q || $sql$
                            site_names(id, name,  row) AS (
                            select id, name, row_number() over (order by name)
                            from site
                            ),
                              data(id, satellite_id, product_type_id, product_type_description,site, site_id) AS (
                            SELECT
                            P.id,
                            P.satellite_id,
                            PT.id,
                            PT.description,
                            S.name,
                            S.id
                             $sql$;
                        END IF;

                         q := q || $sql$
                            FROM product P
                            JOIN product_type_names PT ON P.product_type_id = PT.id
                            JOIN processor PR ON P.processor_id = PR.id
                            JOIN site_names S ON P.site_id = S.id
                            WHERE TRUE -- COALESCE(P.is_archived, FALSE) = FALSE
                            AND EXISTS (
                                SELECT * FROM season WHERE season.site_id = P.site_id AND P.created_timestamp BETWEEN season.start_date AND season.end_date
                            $sql$;
                            IF $4 IS NOT NULL THEN
                            q := q || $sql$
                            AND season.id=$4
                            $sql$;
                            END IF;

                            q := q || $sql$
                            )
                            $sql$;
                             raise notice '%', _site_id;raise notice '%', _product_type_id;raise notice '%', _satellit_id;
                            IF $2 IS NOT NULL THEN
                            q := q || $sql$
                            AND P.site_id = ANY($2)

                            $sql$;
                            END IF;

                            IF $3 IS NOT NULL THEN
                            q := q || $sql$
                            AND P.product_type_id= ANY($3)

                            $sql$;
                            END IF;

                        IF $6 IS NOT NULL THEN
                        q := q || $sql$
                            AND P.created_timestamp >= to_timestamp(cast($6 as TEXT),'YYYY-MM-DD HH24:MI:SS')
                            $sql$;
                        END IF;

                        IF $7 IS NOT NULL THEN
                        q := q || $sql$
                            AND P.created_timestamp <= to_timestamp(cast($7 as TEXT),'YYYY-MM-DD HH24:MI:SS') + interval '1 day'
                            $sql$;
                        END IF;

                        IF $8 IS NOT NULL THEN
                        q := q || $sql$
                            AND P.tiles <@$8 AND P.tiles!='{}'
                            $sql$;
                        END IF;

                        q := q || $sql$
                            ORDER BY S.row, PT.row, P.name
                            )
                        --         select * from data;
                            SELECT array_to_json(array_agg(row_to_json(data)), true) FROM data;
                            $sql$;

                            raise notice '%', q;

                            RETURN QUERY
                            EXECUTE q
                            USING _user_name, _site_id, _product_type_id, _season_id, _satellit_id, _since_timestamp, _until_timestamp, _tiles, _get_nodes;
                        END
                        $BODY$
                      LANGUAGE plpgsql STABLE
                      COST 100
                      ROWS 1000;
                    ALTER FUNCTION sp_get_dashboard_products_nodes(character varying, integer[], integer[], smallint, integer[], timestamp with time zone, timestamp with time zone, character varying[], boolean)
                      OWNER TO admin;
                $str$;
                raise notice '%', _statement;
                execute _statement;              
            
-- Update functions for updating the end timestamp of the job (also to correctly display the end date of the job in the monitoring tab)
                _statement := $str$                        
                    CREATE OR REPLACE FUNCTION sp_mark_job_finished(
                    IN _job_id int
                    ) RETURNS void AS $$
                    BEGIN

                        UPDATE job
                        SET status_id = 6, --Finished
                        status_timestamp = now(),
                        end_timestamp = now()
                        WHERE id = _job_id; 

                    END;
                    $$ LANGUAGE plpgsql;
                $str$;
                raise notice '%', _statement;
                execute _statement;                

                _statement := $str$                        
                    CREATE OR REPLACE FUNCTION sp_mark_job_cancelled(
                    IN _job_id int
                    ) RETURNS void AS $$
                    BEGIN

                        IF (SELECT current_setting('transaction_isolation') NOT ILIKE 'REPEATABLE READ') THEN
                            RAISE EXCEPTION 'The transaction isolation level has not been set to REPEATABLE READ as expected.' USING ERRCODE = 'UE001';
                        END IF;

                        UPDATE step
                        SET status_id = 7, --Cancelled
                        status_timestamp = now()
                        FROM task
                        WHERE task.id = step.task_id AND task.job_id = _job_id
                        AND step.status_id NOT IN (6, 8) -- Finished or failed steps can't be cancelled
                        AND step.status_id != 7; -- Prevent resetting the status on serialization error retries.

                        UPDATE task
                        SET status_id = 7, --Cancelled
                        status_timestamp = now()
                        WHERE job_id = _job_id
                        AND status_id NOT IN (6, 8) -- Finished or failed tasks can't be cancelled
                        AND status_id != 7; -- Prevent resetting the status on serialization error retries.

                        UPDATE job
                        SET status_id = 7, --Cancelled
                        status_timestamp = now(),
                        end_timestamp = now()
                        WHERE id = _job_id
                        AND status_id NOT IN (6, 7, 8); -- Finished or failed jobs can't be cancelled
                    END;
                    $$ LANGUAGE plpgsql;
                $str$;
                raise notice '%', _statement;
                execute _statement;                

                _statement := $str$                        
                    CREATE OR REPLACE FUNCTION sp_mark_job_failed(
                    IN _job_id int
                    ) RETURNS void AS $$
                    BEGIN
                        -- Remaining tasks should be cancelled; the task that has failed has already been marked as failed.
                        UPDATE task
                        SET status_id = 7, -- Cancelled
                        status_timestamp = now()
                        WHERE job_id = _job_id
                        AND status_id NOT IN (6, 7, 8); -- Finished, cancelled or failed tasks can't be cancelled

                        UPDATE job
                        SET status_id = 8, -- Error
                        status_timestamp = now(),
                        end_timestamp = now()
                        WHERE id = _job_id;

                    END;
                    $$ LANGUAGE plpgsql;
                $str$;
                raise notice '%', _statement;
                execute _statement;                
            end if;
            
            
            _statement := 'update meta set version = ''2.0.3'';';
            raise notice '%', _statement;
            execute _statement;
        end if;
    end if;
    raise notice 'complete';
end;
$migration$;

commit;
