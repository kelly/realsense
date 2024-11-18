const RealSenseCamera = require('../index');

const camera = new RealSenseCamera();

camera.on('frame', (frame) => {
    console.log(`Received frame: ${frame.width}x${frame.height}`);

    const depthArray = new Uint16Array(frame.data.buffer);
    console.log('First 10 depth values:', depthArray.slice(0, 10));
});

camera.on('end', () => {
    console.log('Streaming ended.');
});

camera.start();

// Stop streaming after 10 seconds
setTimeout(() => {
    camera.stop();
}, 10000);