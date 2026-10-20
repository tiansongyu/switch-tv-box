(defproject
  griff/airplay-spec-js
  "0.1.0-SNAPSHOT"
  :repositories
  [["clojars" {:url "https://repo.clojars.org/"}]
   ["maven-central" {:url "https://repo1.maven.org/maven2"}]]
  :dependencies
  [[adzerk/boot-cljs "2.1.5"]
   [adzerk/boot-cljs-repl "0.4.0"]
   [boot-deps "0.1.9"]
   [bendyorke/boot-postcss "0.0.1" :scope "test"]
   [adzerk/boot-reload "0.6.0" :scope "test"]
   [pandeiro/boot-http "0.8.3" :scope "test"]
   [cider/piggieback "0.3.9" :scope "test"]
   [weasel "0.7.0" :scope "test"]
   [nrepl "0.4.5" :scope "test"]
   [cljsjs/boot-cljsjs "0.10.4" :scope "test"]
   [griff/boot2nix "1.1.0" :scope "test"]
   [org.clojure/clojure "1.10.1"]
   [onetom/boot-lein-generate "0.1.3" :scope "test"]
   [org.clojure/clojurescript "1.10.597"]
   [sablono "0.8.6"]
   [prismatic/dommy "1.1.0"]
   [reagent "0.9.0-rc4"]
   [re-frame "0.11.0-rc3"]
   [cljs-ajax "0.8.0"]
   [compassus "1.0.0-alpha3"]
   [bidi "2.1.6"]
   [kibu/pushy "0.3.8"]
   [org.clojure/core.async "0.6.532"]
   [camel-snake-kebab "0.4.1"]
   [cljsjs/react-bootstrap "1.0.0-beta.14-0"]
   [cljsjs/react-datepicker "2.3.0-0"]
   [org.clojure/data.xml "0.2.0-alpha6"]
   [devcards "0.2.6"]
   [binaryage/devtools "0.9.11"]]
  :source-paths
  ["src"]
  :resource-paths
  ["libs" "html" "resources"])