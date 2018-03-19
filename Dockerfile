FROM nvidia/cuda:9.0-cudnn7-devel-ubuntu16.04

USER root

RUN echo "deb http://developer.download.nvidia.com/compute/machine-learning/repos/ubuntu1604/x86_64 /" > /etc/apt/sources.list.d/nvidia-ml.list && \
apt-get update && apt-get install -yq --no-install-recommends \
		wget \
    	bzip2 \
    	sudo \
    	locales \
    	fonts-liberation \
        cmake \
        curl \
        ca-certificates \
        libnccl2 \
        libnccl-dev \
        libjpeg-dev \
        libpng-dev \
        build-essential \
	    emacs \
	    git \
	    inkscape \
	    jed \
	    libsm6 \
	    libxext-dev \
	    libxrender1 \
	    lmodern \
	    pandoc \
	    python-dev \
	    texlive-fonts-extra \
	    texlive-fonts-recommended \
	    texlive-generic-recommended \
	    texlive-latex-base \
	    texlive-latex-extra \
	    texlive-xetex \
	    vim \
	    unzip \
     && apt-get clean \
     && rm -rf /var/lib/apt/lists/*

RUN echo "en_US.UTF-8 UTF-8" > /etc/locale.gen && \
    locale-gen

# Install Tini
RUN wget --quiet https://github.com/krallin/tini/releases/download/v0.10.0/tini && \
    echo "1361527f39190a7338a0b434bd8c88ff7233ce7b9a4876f3315c22fce7eca1b0 *tini" | sha256sum -c - && \
    mv tini /usr/local/bin/tini && \
    chmod +x /usr/local/bin/tini

# Configure environment
ENV CONDA_DIR=/opt/conda \
    SHELL=/bin/bash \
    NB_USER=jovyan \
    NB_UID=1000 \
    NB_GID=100 \
    LC_ALL=en_US.UTF-8 \
    LANG=en_US.UTF-8 \
    LANGUAGE=en_US.UTF-8
ENV PATH=$CONDA_DIR/bin:$PATH \
    HOME=/home/$NB_USER

ADD fix-permissions /usr/local/bin/fix-permissions

# Create jovyan user with UID=1000 and in the 'users' group
# and make sure these dirs are writable by the `users` group.
RUN chmod 777 /usr/local/bin/fix-permissions && \
	useradd -m -s /bin/bash -N -u $NB_UID $NB_USER && \
    mkdir -p $CONDA_DIR && \
    chown $NB_USER:$NB_GID $CONDA_DIR && \
    fix-permissions $HOME && \
    fix-permissions $CONDA_DIR

USER $NB_USER

# Setup work directory for backward-compatibility
RUN mkdir /home/$NB_USER/work && \
    fix-permissions /home/$NB_USER

# Install conda as jovyan and check the md5 sum provided on the download site
ENV MINICONDA_VERSION 4.3.30

ENV PYTHON_VERSION=3.6

RUN cd /tmp && \
    wget --quiet https://repo.continuum.io/miniconda/Miniconda3-${MINICONDA_VERSION}-Linux-x86_64.sh && \
    echo "0b80a152332a4ce5250f3c09589c7a81 *Miniconda3-${MINICONDA_VERSION}-Linux-x86_64.sh" | md5sum -c - && \
    /bin/bash Miniconda3-${MINICONDA_VERSION}-Linux-x86_64.sh -f -b -p $CONDA_DIR && \
    rm Miniconda3-${MINICONDA_VERSION}-Linux-x86_64.sh && \
    $CONDA_DIR/bin/conda config --system --prepend channels conda-forge && \
    $CONDA_DIR/bin/conda config --system --set auto_update_conda false && \
    $CONDA_DIR/bin/conda config --system --set show_channel_urls true && \
    $CONDA_DIR/bin/conda update --all --quiet --yes && \
    /opt/conda/bin/conda create -y --name pytorch-py$PYTHON_VERSION python=$PYTHON_VERSION numpy pyyaml scipy ipython mkl&& \
    /opt/conda/bin/conda clean -ya && \
    conda clean -tipsy && \
    fix-permissions $CONDA_DIR


# Install Jupyter Notebook and Hub
RUN conda install --quiet --yes \
    'notebook=5.2.*' \
    'jupyterhub=0.8.*' \
    'jupyterlab=0.29.*' \
    sympy \
    matplotlib \
    scipy \
    numpy \
    sklearn \
    pandas \
    seaborn \
    keras \
    tensorflow \
    theano \
    xgboost \
    && conda clean -tipsy && \
    jupyter labextension install @jupyterlab/hub-extension@^0.6.0 && \
    rm -rf $CONDA_DIR/share/jupyter/lab/staging && \
    fix-permissions $CONDA_DIR

USER root

RUN conda install --name pytorch-py$PYTHON_VERSION -c soumith magma-cuda90

WORKDIR /opt/pytorch
COPY . .

RUN git submodule update --init
RUN TORCH_CUDA_ARCH_LIST="3.5 5.2 6.0 6.1+PTX" TORCH_NVCC_FLAGS="-Xfatbin -compress-all" \
    CMAKE_PREFIX_PATH="$(dirname $(which conda))/../" \
    pip install -v .

RUN git clone https://github.com/pytorch/vision.git && cd vision && pip install -v . \
	&& git clone https://github.com/pytorch/text.git && cd text && pip install -v . 

WORKDIR /workspace
RUN chmod -R a+w /workspace

EXPOSE 8888
WORKDIR $HOME

# Configure container startup
ENTRYPOINT ["tini", "--"]
CMD ["start-notebook.sh"]

# Add local files as late as possible to avoid cache busting
COPY start.sh /usr/local/bin/
COPY start-notebook.sh /usr/local/bin/
COPY start-singleuser.sh /usr/local/bin/
COPY jupyter_notebook_config.py /etc/jupyter/

RUN chmod 777 /etc/jupyter/jupyter_notebook_config.py \
 && chmod 777 /usr/local/bin/start.sh \
 && chmod 777 /usr/local/bin/start-notebook.sh \
 && chmod 777 /usr/local/bin/start-singleuser.sh \
 && fix-permissions /etc/jupyter/

# Switch back to jovyan to avoid accidental container runs as root
USER $NB_USER

