project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-gpu-gl4")
  uuid("da10149d-efb0-44aa-924c-a76a46e1f04d")
  kind("StaticLib")
  language("C++")
  links({
    "elemental-forms",
    "glew",
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-gl",
    "xxhash",
  })
  defines({
    "GLEW_STATIC=1",
    "GLEW_MX=1",
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
  })
  local_platform_files()

-- TODO(benvanik): kill this and move to the debugger UI.
group("src")
project("xenia-gpu-gl4-trace-viewer")
  uuid("450f965b-a019-4ba5-bc6f-99901e5a4c8d")
  kind("WindowedApp")
  language("C++")
  links({
    "beaengine",
    "elemental-forms",
    "gflags",
    "glew",
    "imgui",
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
    "GLEW_STATIC=1",
    "GLEW_MX=1",
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
  })
  files({
    "trace_viewer_main.cc",
    "../../base/main_"..platform_suffix..".cc",
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
      "1>scratch/stdout-trace-viewer.txt",
    })
