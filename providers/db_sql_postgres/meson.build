libpq_dep = dependency('libpq', required: get_option('provider_db_sql_postgres'))

if libpq_dep.found()
  db_sql_postgres_provider_id = 'obi.provider:db.sql.postgres'

  db_sql_postgres_provider = shared_module(
    'obi_provider_db_sql_postgres',
    ['obi_provider_db_sql_postgres_v0.c'],
    include_directories: inc,
    dependencies: deps + [libpq_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [db_sql_postgres_provider]
  provider_profiles += [
    'obi.profile:db.sql-0',
  ]
  provider_profile_pairs += [
    'obi.profile:db.sql-0|' + db_sql_postgres_provider_id,
  ]
endif
