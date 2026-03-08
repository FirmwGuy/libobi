lmdb_dep = dependency('lmdb', required: get_option('provider_db_kv_lmdb'))

if lmdb_dep.found()
  db_kv_lmdb_provider_id = 'obi.provider:db.kv.lmdb'

  db_kv_lmdb_provider = shared_module(
    'obi_provider_db_kv_lmdb',
    ['obi_provider_db_kv_lmdb_v0.c'],
    include_directories: inc,
    dependencies: deps + [lmdb_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [db_kv_lmdb_provider]
  provider_profiles += [
    'obi.profile:db.kv-0',
  ]
  provider_profile_pairs += [
    'obi.profile:db.kv-0|' + db_kv_lmdb_provider_id,
  ]
endif
