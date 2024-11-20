{
  "targets": [
    {
      "target_name": "realsense",
      "sources": ["src/realsense.cpp"],
      "include_dirs": [
        "<!(node -e \"require('nan')\")", 
        "/usr/local/include", 
        "/opt/homebrew/include"
      ],
      "libraries": [
        "-L/usr/local/lib", 
        "-L/opt/homebrew/lib", 
        "-lrealsense2"
      ],
      "cflags": [
        "-std=c++17", 
        "-fexceptions", 
        "-frtti",
        "-fno-exceptions=false"  
      ],
      "cflags_cc": [
        "-std=c++17", 
        "-fexceptions", 
        "-frtti",
        "-fno-exceptions=false" 
      ],
      "xcode_settings": {
        "OTHER_CFLAGS": [
          "-fexceptions",
          "-frtti"
        ],
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      }
    }
  ]
}