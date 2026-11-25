#!/usr/bin/env python3
"""Integration tests for task 4 (the simulator itself is written in C)."""
import os
import re
import signal
import socket
import subprocess
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
os.chdir(ROOT)


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def connect(port, role):
    end = time.monotonic() + 3
    while True:
        try:
            sock = socket.create_connection(('127.0.0.1', port), 0.2)
            sock.sendall(f'ROLE {role}\n'.encode())
            sock.settimeout(0.2)
            return sock
        except (OSError, ConnectionError):
            if time.monotonic() >= end:
                raise
            time.sleep(0.02)


def receive_all(sock, max_time=10):
    data = bytearray()
    deadline = time.monotonic() + max_time
    while time.monotonic() < deadline:
        try:
            block = sock.recv(8192)
            if not block:
                return data.decode()
            data.extend(block)
        except socket.timeout:
            continue
    raise AssertionError('TCP client did not finish')


def launch(port, scenario, delay, **options):
    cmd = ['./fly_tcp_server', '--bind', '0.0.0.0', '--port', str(port),
           '--scenario', scenario, '--startup-ms', '300', '--delay', str(delay), '--quiet']
    for key, value in options.items():
        cmd += ['--' + key.replace('_', '-'), str(value)]
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def finish(server):
    try:
        output, _ = server.communicate(timeout=12)
    except subprocess.TimeoutExpired:
        server.kill()
        server.communicate()
        raise AssertionError('TCP server did not terminate')
    assert server.returncode == 0, (server.returncode, output)
    return output


def test_small():
    port = free_port()
    server = launch(port, 'scenarios/small.txt', 30)
    try:
        observer = connect(port, 'observer')
        drones = connect(port, 'drones')
        all_events = receive_all(observer)
        drone_events = receive_all(drones)
        output = finish(server)
        assert 'Delivered orders: 1' in output, output
        assert 'Total flight distance: 8' in output, output
        assert 'WELCOME observer' in all_events
        assert 'EVENT orders ' in all_events
        assert 'EVENT dispatcher ' in all_events
        assert 'EVENT drones ' in all_events
        assert 'FINISHED tick=8 delivered=1 failed=0 distance=8' in all_events
        assert 'EVENT drones ' in drone_events
        assert 'EVENT orders ' not in drone_events
        assert 'EVENT dispatcher ' not in drone_events
        print('[TCP TEST] small scenario, filtered roles, integrated observer ... OK')
    finally:
        if server.poll() is None:
            server.kill(); server.wait()


def test_dynamic_observers():
    port = free_port()
    server = launch(port, 'scenarios/demo.txt', 30)
    try:
        first = connect(port, 'observer')
        second = connect(port, 'observer')
        orders = connect(port, 'orders')
        time.sleep(0.7)
        first.close()
        time.sleep(0.3)
        third = connect(port, 'observer')
        observer_output = receive_all(second)
        reconnected_output = receive_all(third)
        orders_output = receive_all(orders)
        output = finish(server)
        assert 'Delivered orders: 4' in output, output
        assert 'Failed orders: 2' in output, output
        assert 'Total flight distance: 163' in output, output
        for text in (observer_output, reconnected_output):
            assert 'WELCOME observer' in text
            assert 'STATE tick=' in text
            assert 'DRONE id=' in text and 'ORDER id=' in text
            assert 'FINISHED tick=150 delivered=4 failed=2 distance=163' in text
        assert 'EVENT orders ' in orders_output
        assert 'EVENT drones ' not in orders_output
        print('[TCP TEST] two observers, disconnect/reconnect, demo results ... OK')
    finally:
        if server.poll() is None:
            server.kill(); server.wait()


def test_interrupt():
    port = free_port()
    server = launch(port, 'scenarios/demo.txt', 60)
    try:
        observer = connect(port, 'observer')
        time.sleep(0.7)
        server.send_signal(signal.SIGINT)
        output = receive_all(observer)
        finish(server)
        assert 'FINISHED' in output and 'interrupted=1' in output, output[-500:]
        assert 'Delivered orders: ' in output or 'STATE tick=' in output
        print('[TCP TEST] SIGINT and client shutdown ... OK')
    finally:
        if server.poll() is None:
            server.kill(); server.wait()


if __name__ == '__main__':
    test_small()
    test_dynamic_observers()
    test_interrupt()
    print('[TCP TEST] All 3 network integration checks passed.')
