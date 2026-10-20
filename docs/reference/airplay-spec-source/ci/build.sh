#!/usr/bin/env bash

cd js
boot release
cd -
mdbook build
mkdir book/js
cp js/release/app.js* book/js
