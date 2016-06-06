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

const SOUND_FRAGMENT_SIZE = 1024;  // samples
const SAMPLE_RATE = 44100;  // samples / second
const BUFFERING_TIME = 2.0;  // seconds

class SoundReceiver {
// public:
    constructor(name, address, soundCallback, closedCallback = null) {
        this.ws = new WebSocket(address);
        this.ws.binaryType = 'arraybuffer';
        this.ws.onclose = () => this._onClose();
        this.ws.onmessage = msg => this._onMessage(msg);
        this.ws.onopen = () => this._onOpen();

        this.name = name;
        this.closedCallback = closedCallback;
        this.soundCallback = soundCallback;
    }

// private:
    _onOpen() {
        this.ws.send(JSON.stringify({
            type: 'SUBSCRIBE',
            name: this.name
        }));
    }

    _onClose() {
        if (this.closedCallback !== null) {
            this.closedCallback();
        } else {
            console.warn('Unexpected WebSocket close');
        }
    }

    _onMessage(message) {
        if (!(message.data instanceof ArrayBuffer)) {
            console.warn('Expected ArrayBuffer as message, got ' +
                         message.data.constructor.name);
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

class SoundPlayer {
// public:
    constructor() {
        this.context = new AudioContext();
        if (this.context.sampleRate != SAMPLE_RATE) {
            // TODO: add resampling here or on the backend
            throw new Error('incompatibile sample rate!');
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
        // TODO: add ignoring old samples

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

window.onload = function () {
    const websocketAddrInput = document.getElementById('websocket-addr');
    const deviceNameInput = document.getElementById('device-name');
    const connectBtn = document.getElementById('btn-connect');

    let soundReceiver = null;
    const soundPlayer = new SoundPlayer();

    function enteredWebsocketAddr() {
        if (soundReceiver === null) {
            const addr = websocketAddrInput.value;
            const device = deviceNameInput.value;

            console.log('connecting to ' + addr + ' as ' + device);
            soundReceiver = new SoundReceiver(
                device, addr, samples => soundPlayer.pushSamples(samples),
                () => {
                    soundReceiver = null;
                });
        }
    }

    connectBtn.addEventListener('click', enteredWebsocketAddr);
    websocketAddrInput.addEventListener('keyup', ev => {
        if (ev.keyCode == 13) {
            enteredWebsocketAddr();
        }
    });
}
