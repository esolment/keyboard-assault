add_executable(keyboard_assault main.cpp config.h)
target_compile_features(keyboard_assault PRIVATE cxx_std_17)
target_link_libraries(keyboard_assault PRIVATE user32)
target_link_options(keyboard_assault PRIVATE -mwindows -static -static-libgcc -static-libstdc++)