(ns cards.ui
  (:require [devcards.core :as dc :include-macros true]
    [sablono.core :as sab])
  (:require-macros
   [devcards.core :refer [defcard]]))

(defn start []
  (js/console.log "Start")
  (dc/start-devcard-ui!))

(js/console.log "UI")

(defn reload []
  (js/console.log "Reload"))

(defcard my-first-card
  (sab/html [:h1 "Devcards is freaking awesome!"]))
