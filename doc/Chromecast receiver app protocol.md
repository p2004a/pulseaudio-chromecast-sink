Chromecast receiver app protocol
================================

The Chromecast receiver communicates with backend using custom protocol over
two separate channels.

The first protocol (app protocol) is used to control application and second
one is used to stream sound (WebSocket protocol)

App protocol
------------

App protocol works over virtual channel between sender and receiver that
have to be established anyway. The messages are send between app and sender
under `urn:x-cast:com.p2004a.chromecast-receiver.wsapp` namespace.

It is a simple request response protocol. Every request has form:

```json
{
    "type": "Message type",
    "requestId": 123
}
```

The request might contain additional fields with data needed to fulfill the
request.

Every response might be successful or not. In case of success, returned
message has form:

```json
{
    "type": "OK",
    "requestId": 123,
    "data": null // data returned by request
}
```

In case of failure:

```json
{
    "type": "ERROR",
    "requestId": 123,
    "message": "That request failed because of ..."
}
```

### Message types

#### `START_STREAM`

Request:

```json
{
    "type": "START_STREAM",
    "requestId": 1,
    "deviceName": "my-chromecast-device",
    "addresses": ["ws://134.243.123.312:24133", "ws://193.123.0.412:24133"]
}
```

`deviceName` is the published device name that is needed to connect to correct
audio stream in WebSocket protocol.

`addresses` is a list of possible addresses of audio stream. Chromecast have to
try to connect to every and return failure only when every failed.

Response doesn't contain any additional data.

#### `STOP_STREAM`

Request:

```json
{
    "type": "STOP_STREAM",
    "requestId": 1
}
```

#### `GET_STATE`

Request:

```json
{
    "type": "STOP_STREAM",
    "requestId": 1
}
```

Response

```json
{
    "type": "OK",
    "requestId": 1,
    "data": {
        "state": "STREAMING"
    }
}
```

The returned state can have only two values: `STREAMING` or `NOT_STREAMING`.

WebSocket protocol
------------------

WebSocket protocol is initiated by Chromecast receiver by sending a message:

```json
{
    "type": "SUBSCRIBE",
    "name": "my-chromecast-device"
}
```

In response sound broadcasting server will start to send to Chromecast receiver
binary messages without any header containing sound samples with following spec:

```
rate: 44100
channels: 2
sample format: little endian signed 16bit integer
```
