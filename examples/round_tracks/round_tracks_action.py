# Original example Copyright [2017] [Miles McCoo]
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#    http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# extensively modified by Julian Loiacono, Oct 2018
# updated and repacked as action plugin by Antoine Pintout, May 2019
# rewritten to use kicad-python instead of SWIG by Jon Evans, March 2024

# This script will round PCBNEW traces by shortening traces at intersections,
# and filling new traces between. This process is repeated four times.
# The resulting board is saved in a new file.

import math
import os
import wx
import time
from copy import deepcopy
from typing import Set

from kipy import KiCad
from kipy.errors import ConnectionError
from kipy.board_types import ArcTrack, Track, PadType, BoardLayer
from kipy.geometry import Vector2
from kipy.util import from_mm

from round_tracks_utils import (
    getTrackAngle,
    getTrackAngleDifference,
    reverseTrack,
    shortenTrack,
    similarPoints,
    withinPad,
)
from ui.round_tracks_gui import RoundTracksDialog

RADIUS_DEFAULT = 2.0
PASSES_DEFAULT = 3


class RoundTracks(RoundTracksDialog):
    def __init__(self):
        super(RoundTracks, self).__init__(None)
        self.kicad = KiCad()
        self.board = self.kicad.get_board()
        self.basefilename = os.path.join(
            self.board.document.project.path,
            os.path.splitext(self.board.document.board_filename)[0],
        )

        if self.basefilename.endswith("-rounded"):
            self.basefilename = self.basefilename[: -len("-rounded")]

        self.configfilepath = self.basefilename + ".round-tracks-config"
        self.config = {}
        self.netClassCount = 1
        self.load_config()

        if "checkboxes" not in self.config:
            self.config["checkboxes"] = {
                "new_file": False,
                "native": True,
                "avoid_junctions": False,
            }

        self.do_create.SetValue(self.config["checkboxes"]["new_file"])
        self.use_native.SetValue(self.config["checkboxes"]["native"])
        self.avoid_junctions.SetValue(self.config["checkboxes"]["avoid_junctions"])

        c = self.config["classes"]

        if "Default" not in c:
            self.netclasslist.AppendItem(
                ["Default", True, str(RADIUS_DEFAULT), str(PASSES_DEFAULT)]
            )
        else:
            self.netclasslist.AppendItem(
                [
                    "Default",
                    c["Default"]["do_round"],
                    str(c["Default"]["scaling"]),
                    str(c["Default"]["passes"]),
                ]
            )

        for net_class in self.kicad.get_project(self.board.document).get_net_classes():
            classname = net_class.name
            self.netClassCount += 1
            if classname not in c:
                self.netclasslist.AppendItem(
                    [classname, True, str(RADIUS_DEFAULT), str(PASSES_DEFAULT)]
                )
            else:
                self.netclasslist.AppendItem(
                    [
                        classname,
                        c[classname]["do_round"],
                        str(c[classname]["scaling"]),
                        str(c[classname]["passes"]),
                    ]
                )
        self.validate_all_data()

    def run(self, event):
        start = time.time()
        self.apply.SetLabel("Working...")
        self.validate_all_data()
        self.save_config()

        self.prog = wx.ProgressDialog(
            "Processing",
            "Starting...",
            100,
            self,
            wx.PD_AUTO_HIDE | wx.PD_APP_MODAL | wx.PD_ELAPSED_TIME,
        )

        if self.do_create.IsChecked():
            new_name = self.basefilename + "-rounded.kicad_pcb"
            self.board.save_as(new_name)

        anySelected = False
        for item in self.board.get_selection():
            if isinstance(item, Track):
                anySelected = True
                break

        commit = self.board.begin_commit()

        self.allTracks = self.board.get_tracks()
        self.allVias = self.board.get_vias()
        self.allPads = self.board.get_pads()
        self.selected = self.board.get_selection()

        avoid = self.avoid_junctions.IsChecked()
        classes = self.config["classes"]
        for classname in classes:
            if classes[classname]["do_round"]:
                if self.use_native.IsChecked():
                    self.addIntermediateTracks(
                        scaling=classes[classname]["scaling"],
                        netclass=classname,
                        native=True,
                        onlySelection=anySelected,
                        avoid_junctions=avoid,
                    )
                else:
                    for i in range(classes[classname]["passes"]):
                        self.addIntermediateTracks(
                            scaling=classes[classname]["scaling"],
                            netclass=classname,
                            native=False,
                            onlySelection=anySelected,
                            avoid_junctions=avoid,
                            msg=f", pass {i+1}",
                        )

        self.board.push_commit(commit, "Round Tracks")

        # if m_AutoRefillZones is set, we should skip here, but PCBNEW_SETTINGS is not exposed to swig
        # ZONE_FILLER has SetProgressReporter, but PROGRESS_REPORTER is also not available, so we can't use it
        # even zone.SetNeedRefill(False) doesn't prevent it running twice
        self.prog.Pulse("Rebuilding zones...")
        wx.Yield()

        try:
            self.board.refill_zones()
        except ConnectionError:
            pass

        if bool(self.prog):
            self.prog.Destroy()
            wx.Yield()
        dt = time.time() - start
        if dt > 0.1:
            wx.MessageBox(
                "Done, took {:.3f} seconds".format(time.time() - start), parent=self
            )
        self.EndModal(wx.ID_OK)

    def on_close(self, event):
        self.EndModal(wx.ID_OK)

    def on_item_editing(self, event):
        if bool(self.netclasslist):
            self.validate_all_data()

    def load_config(self):
        new_config = {}
        if os.path.isfile(self.configfilepath):
            with open(self.configfilepath, "r") as configfile:
                for line in configfile.readlines():
                    params = line[:-1].split("\t")
                    new_config_line = {}
                    try:
                        new_config_line["do_round"] = params[1] == "True"
                        new_config_line["scaling"] = float(params[2])
                        new_config_line["passes"] = int(params[3])
                        new_config[params[0]] = new_config_line
                    except Exception:
                        try:
                            new_config_line["new_file"] = params[0] == "True"
                            new_config_line["native"] = params[1] == "True"
                            new_config_line["avoid_junctions"] = params[2] == "True"
                            self.config["checkboxes"] = new_config_line
                        except Exception:
                            pass
        self.config["classes"] = new_config

    def save_config(self):
        classes = self.config["classes"]
        try:
            with open(self.configfilepath, "w") as configfile:
                for classname in classes:
                    configfile.write(
                        "%s\t%s\t%s\t%s\n"
                        % (
                            classname,
                            str(classes[classname]["do_round"]),
                            str(classes[classname]["scaling"]),
                            str(classes[classname]["passes"]),
                        )
                    )
                configfile.write(
                    "%s\t%s\t%s\n"
                    % (
                        str(self.config["checkboxes"]["new_file"]),
                        str(self.config["checkboxes"]["native"]),
                        str(self.config["checkboxes"]["avoid_junctions"]),
                    )
                )
        except PermissionError:
            pass

    def validate_all_data(self):
        new_config = {}
        for i in range(self.netClassCount):
            for j in range(5):
                if j == 2:
                    # param should be between 0 and 1
                    try:
                        tested_val = float(self.netclasslist.GetTextValue(i, j))
                        if tested_val < 0:
                            self.netclasslist.SetTextValue(str(RADIUS_DEFAULT), i, j)
                    except Exception:
                        self.netclasslist.SetTextValue(str(RADIUS_DEFAULT), i, j)
                if j == 3:
                    # param should be between int 1 and 5
                    try:
                        tested_val = int(self.netclasslist.GetTextValue(i, j))
                        if tested_val < 0 or tested_val > 5:
                            self.netclasslist.SetTextValue(str(PASSES_DEFAULT), i, j)
                    except Exception:
                        self.netclasslist.SetTextValue(str(PASSES_DEFAULT), i, j)
            new_config[self.netclasslist.GetTextValue(i, 0)] = {
                "do_round": self.netclasslist.GetToggleValue(i, 1),
                "scaling": float(self.netclasslist.GetTextValue(i, 2)),
                "passes": int(self.netclasslist.GetTextValue(i, 3)),
            }
        self.config["classes"] = new_config
        self.config["checkboxes"] = {
            "new_file": self.do_create.IsChecked(),
            "native": self.use_native.IsChecked(),
            "avoid_junctions": self.avoid_junctions.IsChecked(),
        }

    def addIntermediateTracks(
        self,
        scaling=RADIUS_DEFAULT,
        netclass=None,
        native=False,
        onlySelection=False,
        avoid_junctions=False,
        msg="",
    ):
        # A 90 degree bend will get a maximum radius of this amount
        RADIUS = from_mm(scaling / (math.sin(math.pi / 4) + 1))

        nets = sorted(self.board.get_nets(netclass_filter=netclass),
                      key = lambda net: net.code)
        tracksToRemove = []
        itemsToCreate = []
        tracksModified = []

        progressInterval = int(max(1, len(nets) / 100.0 ))
        lastReport = 0

        for net in nets:
            tracksInNet = [t for t in self.allTracks if t.net == net]
            viasInNet = [v for v in self.allVias if v.net == net]

            tracksPerLayer = {}
            viasPerLayer = {}
            # separate track by layer
            for t in tracksInNet:
                layer = t.layer
                if layer not in tracksPerLayer:
                    tracksPerLayer[layer] = []
                tracksPerLayer[layer].append(t)

            for v in viasInNet:
                # a buried/blind via will report only layers affected
                # a through via will return all 32 possible layers
                layerSet = set(v.padstack.layers)
                for layer in tracksPerLayer:
                    if layer in layerSet:
                        if layer not in viasPerLayer:
                            viasPerLayer[layer] = []
                        viasPerLayer[layer].append(v)

            # TH pads cover all layers
            # SMD/CONN pads only touch F.Cu and B.Cu (layers 0 and 31)
            # Due to glitch in KiCad, pad.GetLayer() always returns 0. Need to use GetLayerSet().Contains() to actually check

            padsInNet = []
            FCuPadsInNet = []
            BCuPadsInNet = []

            for p in self.allPads:
                if p.net == net and (not onlySelection or p in self.selected):
                    if p.pad_type in [PadType.PT_NPTH, PadType.PT_PTH]:
                        padsInNet.append(p)
                    else:
                        if BoardLayer.BL_B_Cu in set(p.padstack.layers):
                            BCuPadsInNet.append(p)
                        else:
                            FCuPadsInNet.append(p)

            for layer in tracksPerLayer:
                tracks = tracksPerLayer[layer]

                if layer in viasPerLayer:
                    vias = viasPerLayer[layer]
                    viaLocations = set([v.position for v in vias])
                else:
                    viaLocations = set()

                # add all the possible intersections to a unique set, for iterating over later
                intersections: Set[Vector2] = set()
                for t1 in range(len(tracks)):
                    for t2 in range(t1 + 1, len(tracks)):
                        # check if these two tracks share an endpoint
                        # reduce it to a 2-part tuple so there are not multiple objects of the same point in the set
                        if (
                            tracks[t1].start == tracks[t2].start
                            or tracks[t1].end == tracks[t2].start
                        ):
                            intersections.add(deepcopy(tracks[t2].start))
                        if (
                            tracks[t1].start == tracks[t2].end
                            or tracks[t1].end == tracks[t2].end
                        ):
                            intersections.add(deepcopy(tracks[t2].end))

                # for each remaining intersection, shorten each track by the same amount, and place a track between.
                trackLengths = {}

                for ip in intersections:
                    (newX, newY) = (ip.x, ip.y)
                    tracksHere = []
                    for t1 in tracks:
                        if similarPoints(t1.start, ip):
                            tracksHere.append(t1)
                        elif similarPoints(t1.end, ip):
                            # flip track such that all tracks start at the IP
                            reverseTrack(t1)
                            tracksHere.append(t1)
                            tracksModified.append(t1)

                    if len(tracksHere) == 0 or (
                        avoid_junctions and len(tracksHere) > 2
                    ):
                        continue

                    # if there are any arcs or vias present, skip the intersection entirely
                    skip = False
                    for t1 in tracksHere:
                        if isinstance(t1, ArcTrack) or ip in viaLocations:
                            skip = True
                            break

                    if skip:
                        continue

                    # If the intersection is within a pad, but none of the tracks end within the pad, skip
                    for p in padsInNet:
                        if withinPad(self.board, p, ip, tracksHere):
                            skip = True
                            break

                    if skip:
                        continue

                    if layer == BoardLayer.BL_F_Cu:
                        for p in FCuPadsInNet:
                            if withinPad(self.board, p, ip, tracksHere):
                                skip = True
                                break
                    elif layer == BoardLayer.BL_B_Cu:
                        for p in BCuPadsInNet:
                            if withinPad(self.board, p, ip, tracksHere):
                                skip = True
                                break

                    if skip:
                        continue

                    shortest = -1
                    for t1 in tracksHere:
                        if id(t1) not in trackLengths:
                            trackLengths[id(t1)] = t1.length()
                        if (
                            shortest == -1
                            or trackLengths[id(t1)] < trackLengths[id(shortest)]
                        ):
                            shortest = t1

                    # sort these tracks by angle, so new tracks can be drawn between them
                    tracksHere.sort(key=getTrackAngle)

                    if native:
                        halfTrackAngle = {}  # cache this, because after shortening the length may end up zero
                        for t1 in range(len(tracksHere)):
                            halfTrackAngle[t1] = (
                                getTrackAngleDifference(
                                    tracksHere[t1],
                                    tracksHere[(t1 + 1) % len(tracksHere)],
                                )
                                / 2
                            )

                        for t1 in range(len(tracksHere)):
                            f = math.sin(halfTrackAngle[t1]) + 1
                            if shortenTrack(
                                tracksHere[t1],
                                min(trackLengths[id(shortest)] * 0.5, RADIUS * f),
                            ):
                                tracksToRemove.append(tracksHere[t1])
                            else:
                                tracksModified.append(tracksHere[t1])

                        for t1 in range(len(tracksHere)):
                            if not (len(tracksHere) == 2 and t1 == 1):
                                theta = math.pi / 2 - halfTrackAngle[t1]
                                f = 1 / (2 * math.cos(theta) + 2)

                                sp = tracksHere[t1].start
                                ep = tracksHere[(t1 + 1) % len(tracksHere)].start

                                if halfTrackAngle[t1] > math.pi / 2 - 0.001:
                                    track = Track()
                                    track.start = sp
                                    track.end = ep
                                    track.width = tracksHere[t1].width
                                    track.layer = tracksHere[t1].layer
                                    track.net = tracksHere[t1].net
                                    itemsToCreate.append(track)
                                else:
                                    mp = Vector2.from_xy(
                                        int(newX * (1 - f * 2) + sp.x * f + ep.x * f),
                                        int(newY * (1 - f * 2) + sp.y * f + ep.y * f),
                                    )
                                    arc = ArcTrack()
                                    arc.start = sp
                                    arc.mid = mp
                                    arc.end = ep
                                    arc.width = tracksHere[t1].width
                                    arc.layer = tracksHere[t1].layer
                                    arc.net = tracksHere[t1].net
                                    itemsToCreate.append(arc)

                    else:
                        # shorten all these tracks
                        for t1 in range(len(tracksHere)):
                            theta = (
                                math.pi / 2
                                - getTrackAngleDifference(
                                    tracksHere[t1],
                                    tracksHere[(t1 + 1) % len(tracksHere)],
                                )
                                / 2
                            )
                            f = 1 / (2 * math.cos(theta) + 2)
                            shortenTrack(
                                tracksHere[t1],
                                min(trackLengths[id(shortest)] * f, RADIUS),
                            )
                            tracksModified.append(tracksHere[t1])

                        # connect the new startpoints in a circle around the old center point
                        for t1 in range(len(tracksHere)):
                            # dont add 2 new tracks in the 2 track case
                            if not (len(tracksHere) == 2 and t1 == 1):
                                track = Track()
                                track.start = tracksHere[t1].start
                                track.end = tracksHere[(t1 + 1) % len(tracksHere)].start
                                track.width = tracksHere[t1].width
                                track.layer = tracksHere[t1].layer
                                track.net = tracksHere[t1].net
                                itemsToCreate.append(track)

            if net.code - lastReport > progressInterval:
                self.prog.Pulse(
                    f"Netclass: {netclass}, {net.code+1} of {len(nets)}{msg}"
                )
                lastReport = net.code

        createdItems = self.board.create_items(itemsToCreate)

        if onlySelection:
            self.board.add_to_selection(createdItems)

        self.board.update_items(tracksModified)
        self.board.remove_items(tracksToRemove)


if __name__ == "__main__":
    app = wx.App()
    rt = RoundTracks()
    rt.ShowModal()
    rt.Destroy()
