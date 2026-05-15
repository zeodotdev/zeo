# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import logging
from abc import ABC, abstractmethod
from typing import Optional, Dict, Type, Any
from google.protobuf.message import Message
from kipy.proto.common.types.base_types_pb2 import KIID
from kipy.util import unpack_any

logger = logging.getLogger(__name__)

_wrapper_registry: Dict[Type[Message], Type['Wrapper']] = {}

def register_wrapper(proto_cls: Type[Message]):
    def decorator(wrapper_cls: Type['Wrapper']):
        _wrapper_registry[proto_cls] = wrapper_cls
        return wrapper_cls
    return decorator

class Wrapper(ABC):
    def __init__(self, proto: Optional[Message] = None, proto_ref: Optional[Message] = None):
        pass

    @property
    def proto(self):
        self._pack()
        return self.__dict__['_proto']

    def _pack(self):
        """Used in some cases to ensure the internal proto state matches the Python
        class instance, for subclasses where the properties are not directly acting on
        the proto object.
        """
        pass

class Item(Wrapper):
    @property
    @abstractmethod
    def id(self) -> KIID:
        return KIID()

def unwrap(message: Any) -> Wrapper:
    concrete = unpack_any(message)
    wrapper_cls = _wrapper_registry.get(type(concrete), None)
    if wrapper_cls:
        return wrapper_cls(proto=concrete)
    logger.debug(f"No wrapper registered for type {type(concrete).__name__}")
    raise ValueError(f"No wrapper registered for type {type(concrete)}")
