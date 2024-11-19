# realsense

Node.js native wrapper for accessing the Intel RealSense D435i camera. 


```js

const RealSenseCamera = require('realsense');
const camera = new RealSenseCamera();

const options = {
    depthWidth: 480,
    depthHeight: 270,
    colorWidth: 424,
    colorHeight: 240,
    fps: 60
};

camera.on('frame', (frame) => {
    const { depthFrame, colorFrame } = frame;

    const depthArray = new Uint16Array(depthFrame.data.buffer);
    const colorArray = new Uint8Array(colorFrame.data.buffer);
})

camera.on('end', () => {
    console.log('Streaming ended.');
});

camera.start(options);

```