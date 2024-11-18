{
  "targets": [
    {
      "target_name": "realsense",
      "sources": [ "./realsense.cpp" ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
        "/usr/local/include"
      ],
      "libraries": [
        "-L/usr/local/lib",
        "-lrealsense2"
      ],
      "cflags": [
        "-std=c++17",
        "-fexceptions",
        "-frtti"
      ],
      "cflags_cc": [
        "-std=c++17",
        "-fexceptions",
        "-frtti"
      ]
    }
  ]
}
