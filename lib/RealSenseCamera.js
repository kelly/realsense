
const { EventEmitter } = require('events');
const realsense = require('bindings')('realsense');

class RealSenseCamera extends EventEmitter {
    constructor() {
        super();
        this.isStreaming = false;
    }

    start() {
        if (this.isStreaming) return;

        this.isStreaming = true;

        realsense.startStreaming(
            (frame) => {
                // Emit 'frame' event with the frame data
                this.emit('frame', frame);
            },
            () => {
                // Streaming has ended
                this.isStreaming = false;
                this.emit('end');
            }
        );
    }

    stop() {
        if (this.isStreaming) {
            realsense.stopStreaming();
            this.isStreaming = false;
        }
    }
}

export default RealSenseCamera;