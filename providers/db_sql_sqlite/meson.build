sqlite_dep = dependency('sqlite3', required: get_option('provider_db_sql_sqlite'))

if sqlite_dep.found()
  db_sql_sqlite_provider_id = 'obi.provider:db.sql.sqlite'

  db_sql_sqlite_provider = shared_module(
    'obi_provider_db_sql_sqlite',
    ['obi_provider_db_sql_sqlite_v0.c'],
    include_directories: inc,
    dependencies: deps + [sqlite_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [db_sql_sqlite_provider]
  provider_profiles += [
    'obi.profile:db.sql-0',
  ]
  provider_profile_pairs += [
    'obi.profile:db.sql-0|' + db_sql_sqlite_provider_id,
  ]
endif
