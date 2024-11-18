import { EventEmitter } from 'events';
import camera from './build/Release/realsense';

class Realsense extends EventEmitter {
    constructor() {
        super();
        this.isStreaming = false;
    }

    start() {
        if (this.isStreaming) return;

        this.isStreaming = true;

        camera.startStreaming(
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
            camera.stopStreaming();
            this.isStreaming = false;
        }
    }
}

module.exports = Realsense;
