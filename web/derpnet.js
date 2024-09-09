(() => {
  "use strict";

  // don't forget to include this script first: https://tweetnacl.js.org/nacl-fast.min.js

  const frameHeaderLength = 1 + 4;
  const derpMagic = new TextEncoder().encode("DERP🔑");
  const maxBufferedSendAmount = 256 * 1024;

  window.derpNet = (derpServer, secretKey, connectedCallback, disconnectedCallback, recvCallback) => {

    const ws = new WebSocket(`wss://${derpServer}/derp`, "derp");
    ws.binaryType = "arraybuffer";

    let serverPublicKey = null;

    let incomingSize = 0;
    const incomingData = new Uint8Array(64*1024);
    const incomingView = new DataView(incomingData.buffer);

    const sharedKeyCache = new Map();

    const getSharedKey = (publicKey) => {
      const cacheKey = String.fromCharCode(...publicKey);
      const sharedKey = sharedKeyCache.get(cacheKey);
      if (sharedKey !== undefined) {
        return sharedKey;
      }
      const newSharedKey = nacl.box.before(publicKey, secretKey);
      sharedKeyCache.set(cacheKey, newSharedKey);
      return newSharedKey;
    };

    const error = () => {
      ws.onclose = null;
      ws.close();
      disconnectedCallback();
    };

    const nextFrame = () => {
      if (incomingSize < frameHeaderLength) {
        return null;
      }

      const frameType = incomingView.getUint8(0);
      const frameSize = incomingView.getUint32(1);
      if (incomingSize < frameHeaderLength + frameSize) {
        return null;
      }
      return [frameType, frameSize, incomingData.subarray(frameHeaderLength, frameHeaderLength + frameSize)];
    };

    const parseFrame = (data) => {
      if (data.byteLength + incomingSize > incomingData.length) {
        // server is sending too much garbage
        return error();
      }
      incomingData.set(new Uint8Array(data), incomingSize);
      incomingSize += data.byteLength;

      return nextFrame();
    };

    const consumeFrame = (frameSize) => {
      incomingSize -= frameHeaderLength + frameSize;
      incomingData.copyWithin(0, frameHeaderLength + frameSize, frameHeaderLength + frameSize + incomingSize);
    };

    const sendClientInfo = () => {
      const nonce = nacl.randomBytes(nacl.box.nonceLength);
      const clientInfo = new TextEncoder().encode(JSON.stringify({"version": 2}));
      const publicKey = nacl.box.keyPair.fromSecretKey(secretKey).publicKey;

      const frameLen = nacl.box.publicKeyLength + nacl.box.nonceLength + nacl.box.overheadLength + clientInfo.length;
      const frame = new Uint8Array(frameHeaderLength + frameLen);
      const view = new DataView(frame.buffer);
      view.setUint8(0, 2); // ClientInfo
      view.setUint32(1, frameLen);
      frame.set(publicKey, frameHeaderLength);
      frame.set(nonce, frameHeaderLength + nacl.box.publicKeyLength);
      frame.set(nacl.box(clientInfo, nonce, serverPublicKey, secretKey), frameHeaderLength + nacl.box.publicKeyLength + nacl.box.nonceLength);
      ws.send(frame);
    };

    const onIncomingFrame = (data) => {
      for (let frame = parseFrame(data); frame !== null; frame = nextFrame()) {
        const [frameType, frameSize, frameData] = frame;
        if (frameType !== 5) {
          // ignore all but RecvPacket frames
          consumeFrame(frameSize);
          continue;
        }

        if (frameSize < nacl.box.publicKeyLength + nacl.box.nonceLength + nacl.box.overheadLength) {
          // bad RecvPacket frame
          return error();
        }

        const publicKey = frameData.subarray(0, nacl.box.publicKeyLength);
        const sharedKey = getSharedKey(publicKey);

        const nonce = frameData.subarray(nacl.box.publicKeyLength, nacl.box.publicKeyLength + nacl.box.nonceLength);
        const box = frameData.subarray(nacl.box.publicKeyLength + nacl.box.nonceLength);

        const message = nacl.box.open.after(box, nonce, sharedKey);
        if (!message) {
          // cannot decrypt incoming data
          return error();
        }

        consumeFrame(frameSize);
        recvCallback(publicKey, message);
      }
    };

    const onServerInfo = (data) => {
      const frame = parseFrame(data);
      if (frame === null) {
        return;
      }

      const [frameType, frameSize, frameData] = frame;
      if (frameType !== 3 || frameSize < nacl.box.nonceLength + nacl.box.overheadLength) {
        // bad ServerInfo frame
        return error();
      }

      const nonce = frameData.subarray(0, nacl.box.nonceLength);
      const box = frameData.subarray(nacl.box.nonceLength);

      const message = nacl.box.open(box, nonce, serverPublicKey, secretKey);
      if (message === null) {
        // cannot decrypt ServerInfo frame
        return error();
      }

      const serverInfo = JSON.parse(new TextDecoder().decode(message));
      if (!("version" in serverInfo)) {
        // bad ServerInfo json
        return error();
      }

      ws.onmessage = (event) => onIncomingFrame(event.data);
      consumeFrame(frameSize);
      connectedCallback();
    };

    const onServerKey = (data) => {
      const frame = parseFrame(data);
      if (frame === null) {
        return;
      }

      const [frameType, frameSize, frameData] = frame;
      if (frameType !== 1 || frameSize < derpMagic.length + nacl.box.publicKeyLength) {
        // bad ServerKey frame
        return error();
      }

      if (indexedDB.cmp(derpMagic, frameData.subarray(0, derpMagic.length)) !== 0) {
        // bad ServerKey frame
        return error();
      }

      serverPublicKey = frameData.slice(derpMagic.length, derpMagic.length + nacl.box.publicKeyLength);

      ws.onmessage = (event) => onServerInfo(event.data);
      consumeFrame(frameSize);
      sendClientInfo();
    };

    ws.onclose = (event) => disconnectedCallback();
    ws.onerror = (event) => disconnectedCallback();
    ws.onmessage = (event) => onServerKey(event.data);

    return {

      close: () => {
        ws.onclose = null;
        ws.close();
      },

      send: (targetUserPublicKey, message, doneCallback) => {
        const sharedKey = getSharedKey(targetUserPublicKey);
        const nonce = nacl.randomBytes(nacl.box.nonceLength);

        const frameLen = nacl.box.publicKeyLength + nacl.box.nonceLength + nacl.box.overheadLength + message.length;
        const frame = new Uint8Array(frameHeaderLength + frameLen);
        const view = new DataView(frame.buffer);
        view.setUint8(0, 4); // SendPacket
        view.setUint32(1, frameLen);
        frame.set(targetUserPublicKey, frameHeaderLength);
        frame.set(nonce, frameHeaderLength + nacl.box.publicKeyLength);
        frame.set(nacl.secretbox(message, nonce, sharedKey), frameHeaderLength + nacl.box.publicKeyLength + nacl.box.nonceLength);
        ws.send(frame);

        if (doneCallback) {
          const idle = () => {
            if (ws.bufferedAmount < maxBufferedSendAmount) {
              doneCallback();
            } else {
              requestIdleCallback(idle);
            }
          };
          idle();
        }
      },
    };
  };

  window.derpNet.generateKey = () => {
    return nacl.box.keyPair().secretKey;
  };

  window.derpNet.getPublicKey = (secretKey) => {
    return nacl.box.keyPair.fromSecretKey(secretKey).publicKey;
  };


})();
