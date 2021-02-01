REM set HEADERS=%TM_SDK_DIR%/headers

set HEADERS=C:/Work/themachinery
set FLAGS=-I %HEADERS% -Wno-microsoft-anon-tag -fms-extensions

zig cc -shared -o plugins/custom_component/bin/Debug/tm_custom_component.dll plugins/custom_component/custom_component.c %FLAGS%
zig cc -shared -o plugins/custom_tab/bin/Debug/tm_custom_tab.dll plugins/custom_tab/custom_tab.c %FLAGS%
zig cc -shared -o plugins/gameplay/empty/bin/Debug/tm_gameplay_sample_empty.dll plugins/gameplay/empty/gameplay_sample_empty.c %FLAGS%
zig cc -shared -o plugins/gameplay/first_person/bin/Debug/tm_gameplay_sample_first_person.dll plugins/gameplay/first_person/gameplay_sample_first_person.c %FLAGS%
zig cc -shared -o plugins/gameplay/interaction_system/bin/Debug/tm_gameplay_sample_interaction_system.dll plugins/gameplay/interaction_system/interactable_component.c %FLAGS%
zig cc -shared -o plugins/gameplay/third_person/bin/Debug/tm_gameplay_sample_third_person.dll plugins/gameplay/third_person/gameplay_sample_third_person.c %FLAGS%
zig cc -shared -o plugins/minimal/bin/Debug/tm_minimal.dll plugins/minimal/minimal.c %FLAGS%
zig cc -shared -o plugins/ray_tracing/hello_triangle/bin/Debug/tm_ray_tracing_sample_hello_world.dll plugins/ray_tracing/hello_triangle/ray_tracing_test.c %FLAGS%

zig cc -target x86_64-linux-gnu -shared -o plugins/custom_component/bin/Debug/tm_custom_component.so plugins/custom_component/custom_component.c %FLAGS%
zig cc -target x86_64-linux-gnu -shared -o plugins/custom_tab/bin/Debug/tm_custom_tab.so plugins/custom_tab/custom_tab.c %FLAGS%
zig cc -target x86_64-linux-gnu -shared -o plugins/gameplay/empty/bin/Debug/tm_gameplay_sample_empty.so plugins/gameplay/empty/gameplay_sample_empty.c %FLAGS%
zig cc -target x86_64-linux-gnu -shared -o plugins/gameplay/first_person/bin/Debug/tm_gameplay_sample_first_person.so plugins/gameplay/first_person/gameplay_sample_first_person.c %FLAGS%
zig cc -target x86_64-linux-gnu -shared -o plugins/gameplay/interaction_system/bin/Debug/tm_gameplay_sample_interaction_system.so plugins/gameplay/interaction_system/interactable_component.c %FLAGS%
zig cc -target x86_64-linux-gnu -shared -o plugins/gameplay/third_person/bin/Debug/tm_gameplay_sample_third_person.so plugins/gameplay/third_person/gameplay_sample_third_person.c %FLAGS%
zig cc -target x86_64-linux-gnu -shared -o plugins/minimal/bin/Debug/tm_minimal.so plugins/minimal/minimal.c %FLAGS%
zig cc -target x86_64-linux-gnu -shared -o plugins/ray_tracing/hello_triangle/bin/Debug/tm_ray_tracing_sample_hello_world.so plugins/ray_tracing/hello_triangle/ray_tracing_test.c %FLAGS%