from cx_Freeze import setup, Executable

# Dependencies are automatically detected, but it might need
# fine tuning.
build_options = {'packages': [], 'excludes': []}

base = 'gui'

executables = [
    Executable('main.py', base=base, target_name = 'Updated-Newer-Fresher-Version-3')
]

setup(name='UNF-v3',
      version = '0.1.0-alpha',
      description = 'DoPy Billing Software',
      options = {'build_exe': build_options},
      executables = executables)
