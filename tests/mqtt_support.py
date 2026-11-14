"""Private loopback brokers/subscribers and a wire-level ACK fault peer. No board access."""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time


def wait_for(predicate, timeout=8):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            result=predicate()
            if result:
                return result
        except (FileNotFoundError,json.JSONDecodeError,ConnectionError):
            pass
        time.sleep(.02)
    raise AssertionError('condition timed out')


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0))
        return sock.getsockname()[1]


def rows(path):
    return [json.loads(s) for s in Path(path).read_text().splitlines()]


class Broker:
    def __init__(self, root, port=None):
        self.root=Path(root);self.root.mkdir()
        self.port=port or free_port();self.proc=None;self.generation=0;self.subs=[]
        self.config=self.root/'broker.conf'
        self.config.write_text(f'listener {self.port} 127.0.0.1\nallow_anonymous true\npersistence false\nlog_type all\n')

    def start(self):
        self.generation+=1
        self.log=self.root/f'broker-{self.generation}.log'
        with self.log.open('wb') as log:
            self.proc=subprocess.Popen(['mosquitto','-c',str(self.config),'-v'],stdout=log,stderr=subprocess.STDOUT)
        def ready():
            assert self.proc.poll() is None,self.log.read_text()
            return 'running' in self.log.read_text()
        wait_for(ready)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate();self.proc.wait(timeout=5)

    def subscribe(self, name, topic='lab/#'):
        output=self.root/(name+'.jsonl');error=self.root/(name+'-stderr.txt')
        client='sub-'+name
        with output.open('wb') as out,error.open('wb') as err:
            proc=subprocess.Popen(['mosquitto_sub','-h','127.0.0.1','-p',str(self.port),'-i',client,
                '-t',topic,'-q','1','-F','%J'],stdout=out,stderr=err)
        self.subs.append(proc)
        wait_for(lambda: f'Sending SUBACK to {client}' in self.log.read_text())
        return output

    def close(self):
        for proc in self.subs:
            if proc.poll() is None:
                proc.terminate();proc.wait(timeout=5)
        self.stop()


def packet(sock):
    def exact(n):
        data=b''
        while len(data)<n:
            chunk=sock.recv(n-len(data))
            if not chunk:
                raise EOFError()
            data+=chunk
        return data
    header=exact(1)[0];length=0;factor=1
    for _ in range(4):
        byte=exact(1)[0];length+=(byte&127)*factor;factor*=128
        if byte<128:
            return header,exact(length)
    raise AssertionError('invalid MQTT remaining length')


class FaultPeer:
    """Acknowledges CONNECT (optional) and PING but deliberately withholds PUBACK."""
    def __init__(self, root, connack=True):
        self.root=Path(root);self.root.mkdir();self.connack=connack
        self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen()
        self.listener.settimeout(.1);self.port=self.listener.getsockname()[1]
        self.stop=threading.Event();self.connections=[];self.error=None
        self.thread=threading.Thread(target=self.run);self.thread.start()

    def run(self):
        try:
            with (self.root/'wire.jsonl').open('w',buffering=1) as stream:
                while not self.stop.is_set():
                    try:
                        sock,_=self.listener.accept()
                    except socket.timeout:
                        continue
                    self.connections.append([]);index=len(self.connections)
                    with sock:
                        sock.settimeout(.2)
                        while not self.stop.is_set():
                            try:
                                header,data=packet(sock)
                            except socket.timeout:
                                continue
                            except (EOFError,ConnectionError):
                                break
                            entry=dict(connection=index,header=header,monotonic_s=time.monotonic(),bytes=len(data))
                            if header>>4==1 and self.connack:
                                sock.sendall(b'\x20\x02\x00\x00')
                            elif header>>4==12:
                                sock.sendall(b'\xd0\x00')
                            elif header>>4==3:
                                length=struct.unpack('!H',data[:2])[0]
                                entry.update(topic=data[2:2+length].decode(),mid=struct.unpack('!H',data[2+length:4+length])[0],
                                             payload=json.loads(data[4+length:]))
                            self.connections[-1].append(entry);stream.write(json.dumps(entry)+'\n')
        except Exception as error:
            self.error=repr(error)

    def close(self):
        self.stop.set();self.thread.join(timeout=3);self.listener.close()
        assert not self.thread.is_alive()
        assert self.error is None,self.error
