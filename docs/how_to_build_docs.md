# How to Build the IGT GPU Tools Documentation

This guide provides instructions on how to build the documentation
for the IGT GPU Tools project using MkDocs.
This effort aims to enhance the organization and presentation of our documentation,
making it easier for users and contributors to navigate and understand the project.

## Prerequisites

Before you begin, ensure you have Python installed on your system.
It is strongly recommended to use a virtual environment for the local installation
of required packages.
Please refer to your OS/distro instructions for setting up a virtual environment.

## Setting Up the Virtual Environment

1. **Create a virtual environment:**

   ```bash
   $ python3 -m venv venv
   ```

2. **Activate the virtual environment:**

   ```bash
   $ source ./venv/bin/activate
   ```

## Installing Requirements

Install the necessary packages required to build the documentation:

```bash
$ pip3 install -r ./docs/requirements-docs.txt
```

## Building the Documentation

To build the documentation, execute the following command:

```bash
$ mkdocs build
```

## Hosting Locally

To host the documentation locally and review it in your browser, start the MkDocs server:

```bash
$ mkdocs serve
```

Then, open your favorite browser and navigate to:
[http://127.0.0.1:8000/](http://127.0.0.1:8000/)
