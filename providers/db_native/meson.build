db_native_provider_id = 'obi.provider:db.inhouse'

db_native_provider = shared_module(
  'obi_provider_db_native',
  ['obi_provider_db_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [db_native_provider]
provider_profiles += [
  'obi.profile:db.kv-0',
  'obi.profile:db.sql-0',
]
provider_profile_pairs += [
  'obi.profile:db.kv-0|' + db_native_provider_id,
  'obi.profile:db.sql-0|' + db_native_provider_id,
]
