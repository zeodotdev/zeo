import math
from copy import deepcopy
from math import pi
from typing import Sequence, Union
from kipy.geometry import Vector2
from kipy.board import Board
from kipy.board_types import Track, Arc, Pad

tolerance = 10  # in nanometres


def reverseTrack(track: Union[Track, Arc]):
    ep = deepcopy(track.start)
    track.start = track.end
    track.end = ep


# determines whether 2 points are close enough to be considered identical
def similarPoints(p1: Vector2, p2: Vector2):
    return ((p1.x > p2.x - tolerance) and (p1.x < p2.x + tolerance)) and (
        (p1.y > p2.y - tolerance) and (p1.y < p2.y + tolerance)
    )


# test if an intersection is within the bounds of a pad
def withinPad(board: Board, pad: Pad, a: Vector2, tracks: Sequence[Track]):
    if not board.hit_test(pad, a):
        return False

    # If the intersection is within the pad, return true
    # But if one of the connected tracks is *entirely* within the pad, return false, since rounding won't break connectivity
    inside = True
    for t in tracks:
        if board.hit_test(pad, t.end):
            inside = False
    return inside


# shortens a track by an arbitrary amount, maintaining the angle and the endpoint
def shortenTrack(t1: Track, amountToShorten):
    # return true if amount to shorten exceeds length

    if amountToShorten + tolerance >= t1.length():
        t1.start = t1.end
        return True

    angle = normalizeAngle(getTrackAngle(t1))
    newX = t1.start.x + math.cos(angle) * amountToShorten
    newY = t1.start.y + math.sin(angle) * amountToShorten
    t1.start = Vector2.from_xy(int(newX), int(newY))
    return False


# normalizes any angle to [-pi, pi)
def normalizeAngle(inputAngle):
    while inputAngle >= pi:
        inputAngle -= 2 * pi
    while inputAngle < -pi:
        inputAngle += 2 * pi

    return inputAngle


# gets the angle of a track (unnormalized)
def getTrackAngle(t1: Track):
    # use atan2 so the correct quadrant is returned
    return math.atan2((t1.end.y - t1.start.y), (t1.end.x - t1.start.x))


# Get angle between tracks, assumes both start at their intersection
def getTrackAngleDifference(t1: Track, t2: Track):
    a1 = math.atan2(t1.end.y - t1.start.y, t1.end.x - t1.start.x)
    a2 = math.atan2(t2.end.y - t2.start.y, t2.end.x - t2.start.x)
    t = a1 - a2
    if t > pi:
        t = 2 * pi - t
    if t < -pi:
        t = -2 * pi - t
    return abs(t)
