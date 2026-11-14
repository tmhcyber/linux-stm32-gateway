"""Stage 4 MQTT integration. Every network endpoint is a private loopback child."""
from contextlib import closing
import json
import os
from pathlib import Path
import pty
import signal
import socket
import sqlite3
import subprocess
import tempfile
import time
import unittest

from mqtt_support import Broker, FaultPeer, free_port, rows, wait_for
from process_audit import audit_file_child, audit_process, ChildRuntimeError
from test_gateway import frame
from test_storage import compare_database


@unittest.skipUnless(os.name=='posix' and os.environ.get('GATEWAY'),'Linux make test required')
class MqttTests(unittest.TestCase):
    def setUp(self):
        if os.environ.get('MQTT_EVIDENCE'):
            self.root=Path(os.environ['MQTT_EVIDENCE'])/self._testMethodName;self.root.mkdir(parents=True)
        else:
            tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup);self.root=Path(tmp.name)
        self.children=[];self.resources=[];self.addCleanup(self.cleanup)

    def cleanup(self):
        errors=[]
        for proc,master,slave,error,out in self.children:
            try:
                if proc.poll() is None:
                    proc.terminate();proc.wait(timeout=6)
                audit_file_child(proc,error,self.id())
            except Exception as failure:
                errors.append(failure)
            finally:
                os.close(master);os.close(slave)
        for item in reversed(self.resources):
            try:
                item.close()
            except Exception as failure:
                errors.append(failure)
        if errors:
            raise ExceptionGroup('MQTT cleanup/outcomes',errors)

    def broker(self,port=None):
        b=Broker(self.root/'broker',port);self.resources.append(b);b.start();return b

    def start(self,port,db=True,duration=60):
        master,slave=pty.openpty();out=self.root/'run';error=self.root/'gateway-stderr.txt'
        command=[os.environ['GATEWAY'],'--port',os.ttyname(slave),'--duration',str(duration),'--output',str(out),
                 '--mqtt','--mqtt-port',str(port)]
        if db:
            command+=['--db',str(self.root/'history.sqlite')]
        (self.root/'command.json').write_text(json.dumps(command,indent=2)+'\n')
        with error.open('wb') as stream:
            proc=subprocess.Popen(command,stderr=stream)
        proc._expected_exit=143;self.children.append((proc,master,slave,error,out))
        def ready():
            if proc.poll() is not None:
                audit_file_child(proc,error,self.id(),startup=True)
            return '"kind":"OPEN"' in (out/'events.jsonl').read_text()
        wait_for(ready)
        os.write(master,b'startup\r\n');return proc,master,out

    @staticmethod
    def status(out):
        return json.loads((out/'mqtt-status.json').read_text())

    def stop(self,proc,out,expected=143):
        proc._expected_exit=expected
        if expected==143:
            proc.terminate()
        self.assertEqual(proc.wait(timeout=6),expected)
        return json.loads((out/'summary.json').read_text())

    def check_db(self,out):
        with closing(sqlite3.connect(self.root/'history.sqlite')) as db:
            compare_database(self,db,out)

    def test_content_quality_qos_retained_and_shutdown(self):
        b=self.broker();sub=b.subscribe('live');proc,master,out=self.start(b.port)
        wait_for(lambda:self.status(out)['connected'])
        vectors=[frame(seq=0,quality='NOT_AVAILABLE',status='NOT_RUN',value_ms=0,temperature=0,humidity=0),
                 frame(temperature=-12345,humidity=98765,gap=2,coal=3),
                 frame(quality='STALE',status='NACK',status_ms=2000),
                 frame(quality='OFFLINE',status='TIMEOUT',status_ms=4000),
                 frame(reason='HEARTBEAT',flags='0x00000000')]
        for i,vector in enumerate(vectors,1):
            os.write(master,vector)
            wait_for(lambda:any(r['topic'].endswith('/telemetry') and r['payload']['rx_id']==i for r in rows(sub)))
        actual=[r for r in rows(sub) if r['topic']=='lab/stm32-01/telemetry']
        records=rows(out/'records.jsonl')
        for message,record in zip(actual,records):
            self.assertEqual(message['qos'],1);self.assertEqual(message['retain'],0)
            self.assertEqual({k:message['payload'][k] for k in record},record)
            self.assertEqual(len(message['payload']['gateway_run_id']),32)
            self.assertEqual(message['payload']['gateway_id'],'gw-01')
        self.assertEqual(actual[1]['payload']['points'][0]['value'],-12345)
        self.assertEqual(actual[1]['payload']['points'][0]['scale'],-3)
        late=b.subscribe('late');wait_for(lambda:len(rows(late))>=2)
        self.assertEqual({r['topic'] for r in rows(late)},
            {'lab/stm32-01/device/state','lab/gateway/gw-01/availability'})
        self.assertTrue(all(r['retain']==1 and r['qos']==1 for r in rows(late)))
        summary=self.stop(proc,out);self.check_db(out)
        self.assertTrue(summary['mqtt']['offline_acked']);self.assertEqual(summary['mqtt']['failures'],0)
        self.assertEqual(summary['mqtt']['received'],5)
        self.assertEqual(summary['mqtt']['topics'][0]['pubacks'],5)
        final=b.subscribe('final');wait_for(lambda:len(rows(final))>=2)
        availability=next(r for r in rows(final) if '/availability' in r['topic'])
        self.assertEqual(availability['payload']['status'],'offline');self.assertEqual(availability['retain'],1)

    def test_idle_silence_network_loop_and_thread_roles(self):
        b=self.broker();sub=b.subscribe('idle');proc,master,out=self.start(b.port)
        wait_for(lambda:self.status(out)['connected'])
        bits=(1<<(signal.SIGINT-1))|(1<<(signal.SIGTERM-1));roles=[];tools=[]
        for task in (Path('/proc')/str(proc.pid)/'task').iterdir():
            fields=dict(s.split(':',1) for s in (task/'status').read_text().splitlines() if ':' in s)
            name=(task/'comm').read_text().strip();mask=int(fields['SigBlk'].strip(),16)
            if int(task.name)==proc.pid:
                self.assertEqual(mask&bits,0);roles.append('main')
            elif name in ('gw-storage','gw-mqtt'):
                self.assertEqual(mask&bits,bits);roles.append(name)
            else:
                self.assertTrue(os.environ.get('GATEWAY_TSAN_MODE'));self.assertEqual(name,'gateway');tools.append(task.name)
        self.assertCountEqual(roles,['main','gw-storage','gw-mqtt'])
        (self.root/'threads.json').write_text(json.dumps(dict(roles=roles,tsan_helpers=tools)))
        wait_for(lambda:any(r['topic'].endswith('/device/state') and r['payload']['serial']=='silent' for r in rows(sub)),timeout=19)
        status=self.status(out);self.assertTrue(status['connected']);self.assertEqual(status['failures'],0)
        states=[r['payload'] for r in rows(sub) if r['topic'].endswith('/device/state')]
        self.assertGreaterEqual(len(states),4);self.assertTrue(all(s['legal_frame_age_s'] is None for s in states))
        os.write(master,b'bad\r\n');time.sleep(.3)
        os.write(master,frame());wait_for(lambda:any(r['topic'].endswith('/telemetry') for r in rows(sub)))
        summary=self.stop(proc,out);self.check_db(out)
        self.assertEqual(summary['silent_count'],1);self.assertEqual(summary['mqtt']['failures'],0)

    def test_initial_broker_absent_latest_only_and_mailbox_bound(self):
        port=free_port();proc,master,out=self.start(port,db=False)
        wait_for(lambda:self.status(out)['failures']>=1)
        rss=[]
        for batch in range(20):
            os.write(master,b''.join(frame(seq=batch*500+i,value_ms=(batch*500+i)*1000) for i in range(1,501)))
            rss.append(next(s for s in Path(f'/proc/{proc.pid}/status').read_text().splitlines() if s.startswith('VmRSS:')))
        wait_for(lambda:len(rows(out/'records.jsonl'))==10000)
        before=wait_for(lambda:self.status(out) if self.status(out)['received']==10000 else None)
        self.assertEqual(before['topics'][0]['attempts'],0)
        self.assertGreater(before['topics'][0]['overwritten'],9000)
        self.assertLessEqual(before['pending'],3);self.assertLessEqual(before['high_water'],6)
        (self.root/'rss.json').write_text(json.dumps(rss,indent=2))
        # Install the observer before permitting the one non-retained recovery publish.
        # A subscriber joining after that publish cannot receive it retroactively.
        proc.send_signal(signal.SIGSTOP)
        try:
            b=self.broker(port);sub=b.subscribe('restored')
        finally:
            proc.send_signal(signal.SIGCONT)
        wait_for(lambda:any(r['topic'].endswith('/telemetry') for r in rows(sub)),timeout=34)
        telemetry=[r['payload'] for r in rows(sub) if r['topic'].endswith('/telemetry')]
        self.assertTrue(all(r['rx_id']==10000 for r in telemetry))
        summary=self.stop(proc,out)
        self.assertGreater(summary['mqtt']['recoveries'],0);self.assertGreater(summary['mqtt']['failures'],0)
        self.assertEqual(summary['valid_records'],10000);self.assertTrue(summary['mqtt']['offline_acked'])

    def test_ack_timeout_recreates_client_without_old_queue(self):
        peer=FaultPeer(self.root/'peer');self.resources.append(peer)
        proc,master,out=self.start(peer.port,db=False)
        os.write(master,frame());wait_for(lambda:peer.connections and any(x['header']>>4==3 for x in peer.connections[0]))
        os.write(master,b''.join(frame(seq=i,value_ms=i*1000) for i in range(2,1001)))
        wait_for(lambda:self.status(out)['ack_timeouts']>=1,timeout=8)
        wait_for(lambda:len(peer.connections)>=2,timeout=5)
        wait_for(lambda:any(x.get('topic','').endswith('/telemetry') for x in peer.connections[1]))
        for connection in peer.connections[:2]:
            publishes=[x for x in connection if x['header']>>4==3]
            self.assertLessEqual(len(publishes),3)
            self.assertEqual(len({x['topic'] for x in publishes}),len(publishes))
        telemetry=next(x for x in peer.connections[1] if x.get('topic','').endswith('/telemetry'))
        self.assertEqual(telemetry['payload']['rx_id'],1000)
        started=time.monotonic();summary=self.stop(proc,out)
        self.assertLess(time.monotonic()-started,3.5)
        self.assertFalse(summary['mqtt']['offline_acked']);self.assertLessEqual(summary['mqtt']['high_water'],6)
        self.assertGreater(summary['mqtt']['ack_timeouts'],0)

    def test_connack_deadline_and_bounded_exit(self):
        peer=FaultPeer(self.root/'peer',connack=False);self.resources.append(peer)
        proc,_,out=self.start(peer.port,db=False)
        wait_for(lambda:self.status(out)['connect_timeouts']>=1,timeout=8)
        self.assertEqual(self.status(out)['connacks'],0)
        started=time.monotonic();summary=self.stop(proc,out)
        self.assertLess(time.monotonic()-started,3.5);self.assertFalse(summary['mqtt']['offline_acked'])

    def test_saturated_tcp_listener_has_deadline(self):
        # Linux permits one unaccepted connection with backlog=0. Fill that slot;
        # another SYN would otherwise wait for the kernel's much longer timeout.
        listener=socket.socket();self.addCleanup(listener.close)
        listener.bind(('127.0.0.1',0));listener.listen(0)
        filler=socket.create_connection(listener.getsockname(),timeout=2);self.addCleanup(filler.close)
        proc,_,out=self.start(listener.getsockname()[1],db=False)
        wait_for(lambda:self.status(out)['connect_timeouts']>=1,timeout=4)
        self.assertEqual(self.status(out)['connacks'],0)
        started=time.monotonic();self.stop(proc,out)
        self.assertLess(time.monotonic()-started,3.5)

    def test_natural_end_preserves_mqtt_failure_and_storage(self):
        proc,master,out=self.start(free_port(),duration=2)
        os.write(master,frame())
        summary=self.stop(proc,out,expected=1);self.check_db(out)
        self.assertTrue(summary['complete']);self.assertEqual(summary['signal'],0)
        self.assertEqual(summary['storage']['committed'],1)
        self.assertGreater(summary['mqtt']['failures'],0);self.assertFalse(summary['mqtt']['offline_acked'])

    def test_killed_private_child_delivers_lwt(self):
        b=self.broker();sub=b.subscribe('lwt');proc,_,out=self.start(b.port,db=False)
        wait_for(lambda:any(r['payload'].get('status')=='online' for r in rows(sub)))
        proc._expected_exit=-9;proc.kill();self.assertEqual(proc.wait(timeout=3),-9)
        wait_for(lambda:any(r['payload'].get('reason')=='connection_lost' for r in rows(sub)),timeout=18)
        late=b.subscribe('lwt-late');wait_for(lambda:any(r['payload'].get('reason')=='connection_lost' for r in rows(late)))
        will=next(r for r in rows(late) if r['payload'].get('reason')=='connection_lost')
        self.assertEqual(will['retain'],1);self.assertEqual(will['qos'],1)
        self.assertFalse((out/'summary.json').exists())

    def test_eof_drain_and_mqtt_cli_validation(self):
        b=self.broker();sub=b.subscribe('eof');source=self.root/'input.bin';source.write_bytes(frame())
        base=[os.environ['GATEWAY'],'--replay',str(source),'--boundary','aligned','--output',str(self.root/'eof')]
        result=subprocess.run(base+['--mqtt','--mqtt-port',str(b.port),'--gateway-id','eof-test'],capture_output=True,timeout=5)
        record=audit_process(result.args,result.returncode,result.stderr,0,self.id(),'EOF')
        if record['kind']!='expected_exit':
            raise ChildRuntimeError(record)
        summary=json.loads((self.root/'eof/summary.json').read_text())
        self.assertTrue(summary['mqtt']['offline_acked']);self.assertEqual(summary['mqtt']['topics'][0]['pubacks'],1)
        for args in (['--mqtt-port','1234'],['--gateway-id','x'],['--mqtt','--device-id','bad/topic'],
                     ['--mqtt','--gateway-id','a'*33],['--mqtt','--mqtt-port','65536'],['--mqtt','--mqtt-host','example.com']):
            result=subprocess.run(base+args,capture_output=True)
            record=audit_process(result.args,result.returncode,result.stderr,2,self.id(),'invalid CLI')
            if record['kind']!='expected_exit':
                raise ChildRuntimeError(record)
            self.assertEqual(result.returncode,2)
