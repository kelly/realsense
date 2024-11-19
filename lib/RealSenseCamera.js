
const { EventEmitter } = require('events');
const realsense = require('bindings')('realsense');

class RealSenseCamera extends EventEmitter {
    constructor() {
        super();
        this.isStreaming = false;
    }

    start(options = {}) {
        if (this.isStreaming) return;

        this.isStreaming = true;

        realsense.startStreaming(
            options,
            (frame) => {
                this.emit('frame', frame);
            },
            () => {
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
module.exports = RealSenseCamera;