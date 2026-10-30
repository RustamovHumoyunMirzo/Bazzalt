"""Real loopback SSH handshake and worker protocol, without external servers."""
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from Editor.remote_build import DEFAULT,Test
from bazzalt.platforms import HostPlatform

@unittest.skipUnless(importlib.util.find_spec('paramiko'),'Install the SSH build dependency')
class SSHWorkerTests(unittest.TestCase):
    def test_pinned_host_and_password_authentication(self):
        import paramiko
        key=paramiko.RSAKey.generate(2048);listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);port=listener.getsockname()[1]
        finished=threading.Event()
        class Server(paramiko.ServerInterface):
            def check_auth_password(self,user,password):return paramiko.AUTH_SUCCESSFUL if (user,password)==('builder','fixture-only') else paramiko.AUTH_FAILED
            def get_allowed_auths(self,user):return 'password'
            def check_channel_request(self,kind,channel):return paramiko.OPEN_SUCCEEDED if kind=='session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_exec_request(self,channel,command):
                def run():
                    result=subprocess.run([sys.executable,'-m','Compiler.build','--remote-info'],capture_output=True,cwd=Path(__file__).resolve().parents[2]);channel.sendall(result.stdout);channel.send_exit_status(result.returncode);channel.shutdown_write();finished.set()
                threading.Thread(target=run,daemon=True).start();return True
        def serve():
            connection,_=listener.accept();transport=paramiko.Transport(connection)
            try:
                transport.add_server_key(key);transport.start_server(server=Server());channel=transport.accept(10);finished.wait(15)
            finally:transport.close();listener.close()
        thread=threading.Thread(target=serve,daemon=True);thread.start()
        with tempfile.TemporaryDirectory() as directory:
            known=Path(directory)/'known_hosts';known.write_text(f'[127.0.0.1]:{port} {key.get_name()} {key.get_base64()}\n')
            info=Test(DEFAULT|dict(host='127.0.0.1',port=port,user='builder',authentication='password',known_hosts=str(known),worker_platform=HostPlatform()),'fixture-only',require_tools=False);self.assertEqual(info['protocol'],1);self.assertEqual(info['platform'],HostPlatform())
        thread.join(20);self.assertFalse(thread.is_alive())
