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
        "-std=c++11"
      ],
      "cflags_cc": [
        "-std=c++11"
      ],
      "conditions": []
    }
  ]
}