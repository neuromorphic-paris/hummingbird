newoption {
   trigger = 'without-change-lightcrafter-ip',
   description = 'Do not generate a build configuration for the \'change_lightcrafter_ip\' app'}
newoption {
   trigger = 'without-generate',
   description = 'Do not generate a build configuration for the \'generate\' app'}
newoption {
   trigger = 'without-play',
   description = 'Do not generate a build configuration for the \'play\' app'}
solution 'hummingbird'
    configurations {'release', 'debug'}
    location 'build'
    if _OPTIONS['without-generate'] == nil then
        project 'generate'
            kind 'ConsoleApp'
            language 'C++'
            location 'build'
            files {'source/deinterleave.hpp', 'source/generate.cpp'}
            buildoptions {'-std=c++11'}
            linkoptions {'-std=c++11'}
            configuration 'release'
                targetdir 'build/release'
                defines {'NDEBUG'}
                flags {'OptimizeSpeed'}
            configuration 'debug'
                targetdir 'build/debug'
                defines {'DEBUG'}
                flags {'Symbols'}
    end
    if _OPTIONS['without-change-lightcrafter-ip'] == nil then
        project 'change_lightcrafter_ip'
            kind 'ConsoleApp'
            language 'C++'
            location 'build'
            files {'source/lightcrafter.hpp', 'source/change_lightcrafter_ip.cpp'}
            buildoptions {'-std=c++11'}
            linkoptions {'-std=c++11'}
            configuration 'release'
                targetdir 'build/release'
                defines {'NDEBUG'}
                flags {'OptimizeSpeed'}
            configuration 'debug'
                targetdir 'build/debug'
                defines {'DEBUG'}
                flags {'Symbols'}
    end
    if _OPTIONS['without-play'] == nil then
        project 'play'
            kind 'ConsoleApp'
            language 'C++'
            location 'build'
            files {
                'source/decoder.hpp',
                'source/display.hpp',
                'source/lightcrafter.hpp',
                'source/interleave.hpp',
                'source/play.cpp',
                'third_party/glad/src/glad.cpp'}
            buildoptions {'-std=c++11'}
            linkoptions {'-std=c++11'}
            for path in string.gmatch(
                io.popen('pkg-config --cflags-only-I gstreamermm-1.0'):read('*all'),
                "-I([^%s]+)") do
                includedirs(path)
            end
            linkoptions(io.popen('pkg-config --cflags --libs gstreamermm-1.0'):read('*all'))
            links {'glfw', 'dl', 'pthread'}
            configuration 'release'
                targetdir 'build/release'
                defines {'NDEBUG'}
                flags {'OptimizeSpeed'}
            configuration 'debug'
                targetdir 'build/debug'
                defines {'DEBUG'}
                flags {'Symbols'}
            configuration 'linux'
                links {'GL', 'pthread'}
            configuration 'macosx'
                includedirs {'/usr/local/include'}
                libdirs {'/usr/local/lib'}
                links {'OpenGL.framework'}
    end
