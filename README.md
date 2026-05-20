# NNet - UDP Protocole Réseaux en C

## Présentation

Ce projet est une implémentation d’un protocole réseau fiable basé sur UDP, développé intégralement en C dans une optique de performance, de contrôle mémoire et d’architecture bas niveau.

Le protocole s’inspire fortement des principes de conception de ENet : communication faible latence, fiabilité sélective au-dessus d’UDP, gestion des paquets, retransmission et optimisation des échanges temps réel.

L’objectif du projet est double :

- approfondir les problématiques systèmes et réseaux bas niveau
- démontrer des compétences avancées en conception de structures de données et d’architecture performante en C.

Le protocole est représenté dans [protocole.md](./protocole.md)  
---

# Fonctionnalités

## Protocole réseau

(Quelque note sur le protocole [juste ici](./protocole.md))

- Communication basée sur UDP
- Gestion de la fiabilité au-dessus d’UDP
- Système d’ACK et retransmission
- Gestion des séquences de paquets
- Support des timeouts
- Architecture orientée faible latence
- Design inspiré de ENet

---

# Architecture technique

Le projet repose sur plusieurs composants développés from scratch afin de maximiser les performances et limiter les allocations dynamiques inutiles.

## Time Wheel

Implémentation d’une *time wheel* pour la gestion efficace des timers réseau :

- retransmission des paquets ;
- gestion des timeouts ;
- scheduling d’événements réseau.

Cette structure permet une complexité très faible pour le traitement des timers et évite les coûts d’un système basé sur des heaps ou des listes triées.

---

## Arena Allocator

Système d’allocation mémoire de type *arena allocator* conçu pour :

- réduire la fragmentation mémoire ;
- limiter les appels à `malloc/free` ;
- améliorer la localité mémoire ;
- simplifier la gestion des allocations temporaires.

Approche particulièrement adaptée aux applications avec hot loop.

---

## Circular Buffer à taille fixe

Implémentation d’un buffer circulaire pour :

- le stockage des paquets
- les files d’attente réseau
- les opérations FIFO sans réallocation.

Conçu pour minimiser les copies mémoire et garantir des performances constantes.

---

## Dynamic Array
(the "[dynamic array guy](https://www.youtube.com/watch?v=95M6V3mZgrI)" video)  
Tableau dynamique générique développé en C offrant :  

- redimensionnement automatique 
- API simple et réutilisable.
- faible overhead mémoire 

---

# Objectifs techniques

Ce projet met l’accent sur :

- la programmation système 
- l’optimisation mémoire 
- les structures de données bas niveau 
- la conception de protocoles réseau 
- la performance
- l’architecture logicielle en C.

---

# Stack technique

- Langage : C
- Réseau : UDP / Sockets
- Plateforme cible : Linux
- Gestion mémoire custom
- Structures de données personnalisées

---

# Inspiration

Le protocole est principalement inspiré de :

- ENet
- les problématiques des moteurs de jeux réseau
- les architectures temps réel faible latence

---

# État du projet

Projet actuellement en développement actif.

Fonctionnalités prévues :

- fragmentation/réassemblage de paquets
- canaux fiables/non fiables
- simulation de perte réseau
- outils de benchmark
- statistiques réseau
- tests de charge.

---

# Pourquoi ce projet ?

Ce projet a été conçu afin d’explorer en profondeur :

- les mécanismes internes des protocoles réseau ;
- les contraintes des systèmes temps réel ;
- les techniques d’optimisation utilisées dans les moteurs réseau modernes.

L’objectif est de produire une implémentation performante et maintenable tout en développant une compréhension concrète des problématiques bas niveau.

---

# Compilation

```
make all
```
