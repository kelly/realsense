const RealSenseCamera = require('../index');

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

    console.log(`Received depth frame: ${depthFrame.width}x${depthFrame.height}`);
    console.log(`Received color frame: ${colorFrame.width}x${colorFrame.height}`);

    // Convert depth data Buffer to Uint16Array
    const depthArray = new Uint16Array(depthFrame.data.buffer);

    // Convert color data Buffer to Uint8Array
    const colorArray = new Uint8Array(colorFrame.data.buffer);

    // Access depth values (e.g., first 10 values)
    console.log('First 10 depth values:', depthArray.slice(0, 10));

    // Access color values (e.g., first 10 RGB triplets)
    console.log('First 10 color values:', colorArray.slice(0, 30)); // 10 pixels * 3 channels
});

camera.on('end', () => {
    console.log('Streaming ended.');
});

// Start streaming with custom options
camera.start(options);

// Stop streaming after 5 seconds
setTimeout(() => {
    camera.stop();
}, 5000);