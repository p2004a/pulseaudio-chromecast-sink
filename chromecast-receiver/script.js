/* script.js -- This file is part of pulseaudio-chromecast-sink
 * Copyright (C) 2016  Marek Rusinowski
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

'use strict';

const SOUND_FRAGMENT_SIZE = 4096;  // samples
const SAMPLE_RATE = 48000;  // samples / second
const BUFFERING_TIME = 2.0;  // seconds

class SoundReceiver {
// public:
    constructor(name, address, soundCb, stateCb) {
        try {
            this.name = name;
            this.soundCallback = soundCb;
            this.stateCallback = stateCb;

            this.state = SoundReceiver.State.connecting;

            this.ws = new WebSocket(address);
            this.ws.binaryType = 'arraybuffer';
            this.ws.onclose = () => this._onClose();
            this.ws.onmessage = msg => this._onMessage(msg);
            this.ws.onopen = () => this._onOpen();
            this.ws.onerror = error => this._onError(error);
        } catch (e) {
            _onError(e);
            _onClose();
        }
    }

    close() {
        this.ws.close();
    }

    getState() {
        return this.state;
    }

// private:
    _onError(error) {
        if (this.state === SoundReceiver.State.connecting) {
            this.state = SoundReceiver.State.connecting_failed;
        } else {
            this.state = SoundReceiver.State.error;
        }
        this.stateCallback(this.state);
    }

    _onOpen() {
        this.ws.send(JSON.stringify({
            type: 'SUBSCRIBE',
            name: this.name
        }));
        this.state = SoundReceiver.State.connected;
        this.stateCallback(this.state);
    }

    _onClose() {
        this.state = SoundReceiver.State.closed;
        this.stateCallback(this.state);
    }

    _onMessage(message) {
        if (!(message.data instanceof ArrayBuffer)) {
            console.warn('Expected ArrayBuffer as message, got ' +
                         message.data.constructor.name);
            return;
        }
        const leftChan = new Float32Array(message.data.byteLength / 4);
        const rightChan = new Float32Array(message.data.byteLength / 4);
        const dataView = new DataView(message.data);
        for (let i = 0, j = 0; i < message.data.byteLength; i += 4, j += 1) {
            leftChan[j] = dataView.getInt16(i, true) / 32768.0;
            rightChan[j] = dataView.getInt16(i + 2, true) / 32768.0;
        }

        this.soundCallback({left: leftChan, right: rightChan});
    }
}

SoundReceiver.State = {
    connecting: 'connecting',
    connecting_failed: 'connecting_failed',
    connected: 'connected',
    closed: 'closed',
    error: 'error'
}

class SoundPlayer {
// public:
    constructor() {
        this.context = new AudioContext();
        if (this.context.sampleRate != SAMPLE_RATE) {
            // TODO: add resampling here or on the backend
            console.error('incompatibile sample rate!');
        }

        this.processor = this.context.createScriptProcessor(
            SOUND_FRAGMENT_SIZE, 2, 2);
        this.processor.onaudioprocess = event => this._processAudio(event);

        this.samplesBuffer = [];
        this.partialSamples = null;
        this.partialSamplesOffset = 0;
        this.samplesInLast = 0;
        this.playing = false;

        this.processor.connect(this.context.destination);

        this.nullSample = {
            left: new Float32Array(SOUND_FRAGMENT_SIZE),
            right: new Float32Array(SOUND_FRAGMENT_SIZE)
        };
    }

    pushSamples(samples) {
        let numSamples = samples.left.length;
        let partial = this.partialSamples;
        let offset = this.partialSamplesOffset;

        for (let i = 0; i < numSamples; ++i) {
            if (partial === null) {
                partial = {
                    left: new Float32Array(SOUND_FRAGMENT_SIZE),
                    right: new Float32Array(SOUND_FRAGMENT_SIZE)
                };
            }
            partial.left[offset] = samples.left[i];
            partial.right[offset] = samples.right[i];
            offset += 1;
            if (offset == SOUND_FRAGMENT_SIZE) {
                this.samplesBuffer.push(partial);
                partial = null;
                offset = 0;
            }
        }

        this.partialSamples = partial;
        this.partialSamplesOffset = offset;

        if (!this.playing && this._getTimeInBuffer() >= BUFFERING_TIME) {
            this.playing = true;
        }
    }

// private:
    _popSamples() {
        if (!this.playing) {
            return this.nullSample;
        }

        // Quite brutal but should work fine
        while (this._getTimeInBuffer() >= 1.5 * BUFFERING_TIME) {
            this.samplesBuffer.shift();
        }

        if (this.samplesBuffer.length > 0) {
            return this.samplesBuffer.shift();
        }

        // printing sometimes takes long time
        setTimeout(() => console.warn("Buffer underrun!"));

        this.playing = false;

        if (this.partialSamples !== null) {
            const partial = this.partialSamples;
            this.partialSamples = null;
            this.partialSamplesOffset = 0;
            return partial;
        } else {
            return this.nullSample;
        }
    }

    _getTimeInBuffer() {
        const numSamples = SOUND_FRAGMENT_SIZE * this.samplesBuffer.length +
                           this.partialSamplesOffset;
        return numSamples / SAMPLE_RATE;
    }

    _processAudio(event) {
        const output = event.outputBuffer;
        const samples = this._popSamples();
        output.copyToChannel(samples.left, 0);
        output.copyToChannel(samples.right, 1);
    }
};

function initHTMLControls() {
    const websocketAddrInput = document.getElementById('websocket-addr');
    const deviceNameInput = document.getElementById('device-name');
    const connectBtn = document.getElementById('btn-connect');

    function enteredWebsocketAddr() {
        const addr = websocketAddrInput.value;
        const device = deviceNameInput.value;
        simpleStartStreaming(device, addr);
    }

    connectBtn.addEventListener('click', enteredWebsocketAddr);
    websocketAddrInput.addEventListener('keyup', ev => {
        if (ev.keyCode == 13) {
            enteredWebsocketAddr();
        }
    });
}

function initChromecastReceiver() {
    const appConfig = new cast.receiver.CastReceiverManager.Config();
    appConfig.statusText = 'Websocket Streamer';
    appConfig.maxInactivity = 60;

    const castReceiverManager = cast.receiver.CastReceiverManager.getInstance();
    castReceiverManager.onSenderDisconnected = event => {
        if (castReceiverManager.getSenders().length == 0 && event.reason ==
                cast.receiver.system.DisconnectReason.REQUESTED_BY_SENDER) {
            window.close();
        }
    }

    const websocketAppChannel = castReceiverManager.getCastMessageBus(
        'urn:x-cast:com.p2004a.chromecast-receiver.wsapp',
        cast.receiver.CastMessageBus.MessageType.JSON);

    const message_handlers = {
        'START_STREAM': (message, done) => {
            if (soundReceiver) {
                done('Already streaming', null);
                return;
            }
            if (!(message.addresses instanceof Array)) {
                done('"addresses" atribute is not an Array', null);
                return;
            }
            if (typeof message.deviceName !== 'string') {
                done('"deviceName" atribute is not a string', null);
                return;
            }

            function startStreamRecursive(addrList) {
                if (addrList.length == 0) {
                    done('Connection to every provided endpoint failed');
                    return;
                }

                const addr = addrList.shift();

                function handleStateUpdate(state) {
                    if (state == SoundReceiver.State.closed) {
                        window.soundReceiver = null;
                    } else if (state == SoundReceiver.State.connecting_failed) {
                        startStreamRecursive(addrList);
                    } else if (state == SoundReceiver.State.connected) {
                        done(null, null);
                    }
                }

                window.soundReceiver = new SoundReceiver(
                    message.deviceName, addr,
                    samples => window.soundPlayer.pushSamples(samples),
                    handleStateUpdate);
            }

            startStreamRecursive(message.addresses);
        },
        'STOP_STREAM': (message, done) => {
            if (!soundReceiver) {
                done('Not streaming', null);
                return;
            }

            soundReceiver.close();
            soundReceiver = null;
            done(null, null);
        },
        'GET_STATE': (message, done) => {
            if (soundReceiver) {
                done(null, {state: "STREAMING"});
            } else {
                done(null, {state: "NOT_STREAMING"});
            }
        }
    };

    websocketAppChannel.onMessage = event => {
        let message = event.data;

        function done(errorMessage, data) {
            if (errorMessage) {
                websocketAppChannel.send(event.senderId, {
                    'type': 'ERROR',
                    'requestId': message.requestId,
                    'message': errorMessage
                });
            } else {
                websocketAppChannel.send(event.senderId, {
                    'type': 'OK',
                    'requestId': message.requestId,
                    'data': data
                });
            }
        }

        if (message_handlers[message.type]) {
            message_handlers[message.type](message, done);
        } else {
            done(`Unexpected message type '${message.type}'`, null);
        }
    }

    castReceiverManager.start(appConfig);
}

window.onload = function () {
    // Let's make it global for debugging purposes
    window.soundReceiver = null;
    window.soundPlayer = new SoundPlayer();

    window.simpleStartStreaming = function(device, addr) {
        if (window.soundReceiver !== null) {
            window.soundReceiver.close()
        }
        console.info('connecting to ' + addr + ' as ' + device);
        window.soundReceiver = new SoundReceiver(
            device, addr, samples => window.soundPlayer.pushSamples(samples),
            state => {
                if (state == SoundReceiver.State.closed) {
                    window.soundReceiver = null;
                }
            });
    }

    initHTMLControls();
    initChromecastReceiver();
}
