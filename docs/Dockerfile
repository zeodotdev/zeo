FROM registry.gitlab.com/kicad/kicad-ci/source_containers/master/fedora:38 as build-env
USER root

WORKDIR /src
COPY . ./

RUN git submodule update --init --depth=1
RUN pip install -U sphinx furo
RUN pip install -e .
RUN sphinx-build -b html docs docs/_build

FROM scratch as output-image

COPY --from=build-env /src/docs/_build /docs-html
