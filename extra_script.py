Import("env")
from os.path import join

env.Append(CPPPATH=[
    join(env["PROJECT_DIR"], "include"),
    join(env["PROJECT_DIR"], "lib", "OLED091"),
    join(env["PROJECT_DIR"], "lib", "HapticMux"),
])
