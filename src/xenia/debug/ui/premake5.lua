project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-debug-ui")
  uuid("ed128630-8b4e-4dd7-afc9-ef00052fe3a7")
  kind("WindowedApp")
  language("C++")
  links({
    "beaengine",
    "elemental-forms",
    "gflags",
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-apu-xaudio2",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",
    "xenia-debug",
    "xenia-gpu",
    "xenia-gpu-gl4",
    "xenia-hid-nop",
    "xenia-hid-winkey",
    "xenia-hid-xinput",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-gl",
    "xenia-vfs",
  })
  flags({
    "WinMain",  -- Use WinMain instead of main.
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
  })
  recursive_platform_files()
  files({
    "debugger_main.cc",
    "../../base/main_"..platform_suffix..".cc",
  })
  files({
    "debugger_resources.rc",
  })
  resincludedirs({
    project_root,
    project_root.."/third_party/elemental-forms",
  })

  filter("configurations:Checked")
    local libav_root = "../third_party/libav-xma-bin/lib/Debug"
    linkoptions({
      libav_root.."/libavcodec.a",
      libav_root.."/libavutil.a",
    })
  filter("configurations:Debug or Release")
    local libav_root = "../third_party/libav-xma-bin/lib/Release"
    linkoptions({
      libav_root.."/libavcodec.a",
      libav_root.."/libavutil.a",
    })

  filter("platforms:Windows")
    debugdir(project_root)
    debugargs({
      "--flagfile=scratch/flags.txt",
      "2>&1",
      "1>scratch/stdout-debugger.txt",
    })
