
const { EventEmitter } = require('events');
const realsense = require('bindings')('realsense');

class RealSenseCamera extends EventEmitter {
    constructor() {
        super();
        this.isStreaming = false;
        this._setupSignalHandlers();
    }

    start(options = {}) {
        if (this.isStreaming) return;

        this.isStreaming = true;

        realsense.startStreaming(
            options,
            (frame) => {
                this.emit('frame', frame);
            },
            (err) => {
                this.isStreaming = false;
                if (err) {
                    this.emit('error', err);
                } else {
                    this.emit('end');
                }
            }
        );
    }

    stop() {
        if (this.isStreaming) {
            realsense.stopStreaming();
            this.isStreaming = false;
        }
    }

    _setupSignalHandlers() {
        const signals = ['SIGINT', 'SIGTERM'];
        signals.forEach((signal) => {
            process.on(signal, () => {
                if (this.isStreaming) {
                    this.stop();
                }
                process.exit();
            });
        });

        process.on('uncaughtException', (err) => {
            console.error('Uncaught Exception:', err);
            if (this.isStreaming) {
                this.stop();
            }
            process.exit(1);
        });

        process.on('unhandledRejection', (reason, promise) => {
            console.error('Unhandled Rejection:', reason);
            if (this.isStreaming) {
                this.stop();
            }
            process.exit(1);
        });
    }
}
module.exports = RealSenseCamera;