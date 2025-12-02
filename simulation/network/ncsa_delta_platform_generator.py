#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2025, University of California, Merced. All rights reserved.
#
# This file is part of the simulation software package developed by
# the team members of Prof. Xiaoyi Lu's group at University of California, Merced.
#
# For detailed copyright and licensing information, please refer to the license
# file LICENSE in the top level directory.

import xml.etree.ElementTree as ET
import xml.dom.minidom
import argparse

def create_platform_xml(num_nodes, output_file, bandwidth, latency):
    platform = ET.Element('platform', version='4.1')
    zone = ET.SubElement(platform, 'zone', id='zone0', routing='Full')

    # Create hosts
    for i in range(1, num_nodes + 1):
        ET.SubElement(zone, 'host', id=f'Node-{i}', speed='2445Mf')

    # Create links
    for i in range(1, num_nodes * (num_nodes+1)):
        ET.SubElement(zone, 'link', id=str(i), bandwidth=bandwidth, latency=latency)

    # Create loopback link
    ET.SubElement(zone, 'link', id='loopback', bandwidth=bandwidth, latency='1us', sharing_policy='FATPIPE')

    # Create routes for loopback
    for i in range(1, num_nodes + 1):
        route = ET.SubElement(zone, 'route', src=f'Node-{i}', dst=f'Node-{i}')
        ET.SubElement(route, 'link_ctn', id='loopback')

    # Create routes between nodes
    link_id = 1
    for i in range(1, num_nodes + 1):
        for j in range(i + 1, num_nodes + 1):
            route = ET.SubElement(zone, 'route', src=f'Node-{i}', dst=f'Node-{j}')
            ET.SubElement(route, 'link_ctn', id=str(link_id))
            link_id += 1

    # Convert ElementTree to a string
    xml_str = ET.tostring(platform, encoding='utf-8')
    # Parse string with minidom for pretty printing
    dom = xml.dom.minidom.parseString(xml_str)
    pretty_xml_as_string = dom.toprettyxml(indent="    ")

    # Write the pretty printed XML to a file
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(pretty_xml_as_string)

if __name__ == '__main__':

    parser = argparse.ArgumentParser(description="The generator for the client-server XML")

    parser.add_argument('--num_nodes', type=int, help='Number of nodes to create', required=True, default=128)
    parser.add_argument('--num_clients_per_node', type=int, help='Number of clients per node', required=True, default=64)
    parser.add_argument('--output_file', type=str, help='Output file name', required=True, default=f'delta_client_server_128.xml')
    parser.add_argument('--bandwidth', type=str, help='The bandwidth of the platform', required=False, default='200GBps')
    parser.add_argument('--latency', type=str, help='The latency of the platform', required=False, default='5us')

    args = parser.parse_args()
    num_nodes = args.num_nodes
    num_clients_per_node = args.num_clients_per_node
    output_file = args.output_file
    bandwidth = args.bandwidth
    latency = args.latency

    print(f'Creating platform xml for {num_nodes} nodes')
    create_platform_xml(num_nodes, output_file, bandwidth, latency)
    print(f'Platform XML created at {output_file}')