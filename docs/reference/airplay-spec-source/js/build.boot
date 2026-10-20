(task-options!
  pom {:project  'griff/airplay-spec-js
       :version "0.1.0-SNAPSHOT"})

(set-env!
  :source-paths    #{"src"}
  :resource-paths    #{"html" "libs" "resources"}
  :dependencies '[; Boot setup
                  [adzerk/boot-cljs "2.1.5"]
                  [adzerk/boot-cljs-repl "0.4.0"]
                  [boot-deps "0.1.9"]
                  [bendyorke/boot-postcss "0.0.1" :scope "test"]
                  [adzerk/boot-reload "0.6.0" :scope "test"]
                  [pandeiro/boot-http "0.8.3" :scope "test"]
                  [cider/piggieback "0.3.9" :scope "test"]
                  #_[com.cemerick/piggieback "0.2.2" :scope "test"]
                  [weasel                  "0.7.0"  :scope "test"]
                  #_[org.clojure/tools.nrepl "0.2.13" :scope "test"]
                  [nrepl "0.4.5" :scope "test"]
                  [cljsjs/boot-cljsjs "0.10.4" :scope "test"]
                  [griff/boot2nix "1.1.0" :scope "test"]
                  [org.clojure/clojure "1.10.1"]
                  [onetom/boot-lein-generate "0.1.3" :scope "test"]

                  ; App dependencies
                  [org.clojure/clojurescript   "1.10.597"]
                  [sablono "0.8.6"]
                  [prismatic/dommy "1.1.0"]
                  [reagent "0.9.0-rc4"]
                  [re-frame "0.11.0-rc3"]
                  #_[cljs-react-reload "0.1.1"]
                  [cljs-ajax "0.8.0"]
                  [compassus "1.0.0-alpha3"]
                  [bidi "2.1.6"]
                  [kibu/pushy "0.3.8"]
                  [org.clojure/core.async "0.6.532"]
                  [camel-snake-kebab "0.4.1"]
                  [cljsjs/react-bootstrap "1.0.0-beta.14-0"]
                  [cljsjs/react-datepicker "2.3.0-0"]
                  [org.clojure/data.xml "0.2.0-alpha6"]

                  ; Other dependencies
                  [devcards "0.2.6"]
                  [binaryage/devtools "0.9.11"]])

(require '[boot.lein])
(boot.lein/generate)

(require '[adzerk.boot-reload :refer [reload]]
         '[adzerk.boot-cljs-repl :refer [cljs-repl start-repl]]
         '[adzerk.boot-cljs :refer [cljs]]
         '[griff.boot2nix :refer [boot2nix]]
         '[cljsjs.boot-cljsjs.packaging :refer [deps-cljs]]
         '[bendyorke.boot-postcss :refer [postcss]]
         '[pandeiro.boot-http    :refer [serve]])

(deftask dev []
  (comp (watch)
        (speak)
        (reload :ids #{"app"}
                :only-by-re [#"^app\.out/.*|css/.*|xterm/.*|.*\.html"]
                :on-jsload 'airplay.main/reload)
        (reload :ids #{"app-repl"}
                :only-by-re [#"^app-repl\.out/.*|css/.*|xterm/.*|.*\.html"]
                :on-jsload 'airplay.main/reload
                :asset-host "http://localhost:3002")
        (reload :ids #{"cards"}
                :only-by-re [#"^cards\.out/.*|css/.*|xterm/.*|.*\.html"]
                :on-jsload 'cards.ui/reload)
        (reload :ids #{"cards-repl"}
                :only-by-re [#"^cards-repl\.out/.*|css/.*|xterm/.*|.*\.html"]
                :on-jsload 'cards.ui/reload)
        (cljs-repl :ids #{"app-repl" "cards-repl"})
        (cljs :optimizations :none
              :compiler-options {:devcards true
                                 :preloads '[devtools.preload]})
        (target)
        (serve :dir "target" :port 3002)))

(replace-task!
  [r dev] (fn [& xs]
             ;; we only want to have "dev" included for the REPL task
             (merge-env! :source-paths #{"dev"})
             (apply r xs)))

(deftask prod []
         (comp
           (serve :dir "target/")
           (watch)
           (speak)
           (cljs :ids #{"app" "cards"} :source-map true :optimizations :advanced)
           (target)))

(deftask build []
  (comp
    (cljs :ids #{"app" "cards"} :source-map true :optimizations :advanced)
    (target)))

(deftask release []
  (comp
    (cljs :ids #{"app"} :source-map true :optimizations :advanced)
    (target :dir #{"release/"})))
