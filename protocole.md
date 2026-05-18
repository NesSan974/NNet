
| Code (hex) | Nom                        | Description                                      |
| ---------- | -------------------------- | ------------------------------------------------ |
| `0x01`     | `ACKNOWLEDGE`              | Réponse à un paquet fiable                       |
| `0x02`     | `CONNECT`                  | Demande de connexion                             |
| `0x03`     | `VERIFY_CONNECT`           | Réponse à une connexion                          |
| `0x04`     | `DISCONNECT`               | Déconnexion propre                               |
| `0x05`     | `PING`                     | Garder la connexion en vie                       |
| `0x06`     | `SEND_RELIABLE`            | Envoi fiable                                     |
| `0x07`     | `SEND_UNRELIABLE`          | Envoi non fiable                                 |
| `0x08`     | `SEND_FRAGMENT`            | Fragmentation d’un paquet RELIABLE trop gros     |
| `0x09`     | `SEND_UNSEQUENCED`         | Envoi non ordonné, non fiable (rarement utilisé) |
| `0x0A`     | `BANDWIDTH_LIMIT`          | Limite la bande passante (serveur → client)      |
| `0x0B`     | `THROTTLE_CONFIGURE`       | Configuration du throttle (latence adaptative)   |
| `0x0C`     | `SEND_UNRELIABLE_FRAGMENT` | Fragment non fiable (très peu utilisé)           |

UDP Packet example :
```
┌────────────────────────────────┐
│  Net Packet Header (4 à 10B)   │
│ ┌─ Peer ID                     │ <-- ID de la connexion
│ ├─ Flags                       │ <-- Options (ex : compressed)
│ ├─ commandCount                │ <-- Nb de commande dans le packet 
│ └─ sentTime (opt)              │ <-- timestamp (utiliser pour calcule delta reseaux / ping ms)
├────────────────────────────────┤
│ Command: ACK + flag            │ <-- Ack un précédent Packet Sequence
│ ├─ Channel ID                  │ <-- 
│ ├─ Received Seq Number         │ <-- reliable sequence number qu'on ack
│ └─ Received Sent Time          │ <-- le timestamp lié au 'Received Seq Number'
├────────────────────────────────┤
│ Command: SEND_RELIABLE + flag  │ <-- Commande + flag sur 1 octet
│ ├─ Channel ID                  │ <-- Pour le multiplexage 1 octet
│ ├─ Reliable Seq Number         │ <-- ID unique pour cette commande associé au channel ID
│ ├─ Data Length                 │ <-- SUR 32BIT !!!
│ └─ Payload (tes données)       │
├────────────────────────────────┤
│ Command: SEND_UNRELIABLE + flag│ <-- Commande + flag sur 1 octet
│ ├─ Channel ID                  │ <-- Pour le multiplexage
│ ├─ Unreliable Seq Number (opt) │ <-- ID unique pour cette commande associé au channel ID
│ ├─ Data Length                 │ <-- SUR 32BIT !!!
│ └─ Payload (tes données)       │
└────────────────────────────────┘
```

Exemple d'un paquet fragmenter :
```
┌───────────────────────────────┐
│ ENet Packet Header (4 à 10B)  │
│ ├─ Peer ID                    │ <-- ID de la connexion
│ ├─ Flags                      │ <-- Options
| ├─ commandCount               | <-- Nb de commande dans le packet 
| └─ sentTime (opt)             | <-- timestamp
├───────────────────────────────┤
| Command: ACK + flag           | <-- Ack un précédent Packet Sequence
| ├─Channel ID                  | <-- useless, mais quand meme mis
| ├─Received Seq Number         | <-- Packet Sequence qu'on ack
| └─Received Sent Time          |
├───────────────────────────────┤
│ Command: SEND_FRAGMENT + flag │ <-- Commande sur 1 octet 
│ ├─ Channel ID                 │ <-- Pour le multiplexage 1 octet
│ ├─ Reliable Seq Number        │ <-- ID unique pour cette commande associé au channel ID
| ├─ Start Seq	                │ <-- le 'Reliable Seq Number' du 1er fragment (16 bits)
| ├─ Fragment Count	            │ <-- Nombre total de fragments 32 bits
| ├─ Fragment Number	          │ <-- Numéro de ce fragment 32 bits
| ├─ Total Length	              │ <-- Longueur totale du message reconstitué 32 bits
| ├─ Fragment Offset	          │ <-- 32 bits	Position dans le message global
│ └─ Payload (tes données)      │
└───────────────────────────────┘
```

En header flag il y a 2 flag :
```C
ENET_PROTOCOL_HEADER_FLAG_COMPRESSED = (1 << 14), // si commpressé
ENET_PROTOCOL_HEADER_FLAG_SENT_TIME  = (1 << 15), // Si on envoit le timestamp
ENET_PROTOCOL_HEADER_FLAG_MASK       = ENET_PROTOCOL_HEADER_FLAG_COMPRESSED | ENET_PROTOCOL_HEADER_FLAG_SENT_TIME,
```

A chaque commande il y a 2 flag posible :
```C
ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE = (1 << 7),
ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED = (1 << 6),
```


TODO :
determiner VERIFY_CONNECT et CONNECT

```
├────────────────────────────────┤
│ Command: CONNECT + flag        │
│ ├─ Channel ID                  │ 
│ ├─ Received Seq Number         │
│ ├─ Received Sent Time          │ 
│ └─ Tick Server ?               │
├────────────────────────────────┤


├────────────────────────────────┤
| Command: VERIFY_CONNECT + flag │ 
| ├─ Channel ID                  │ 
| ├─ Received Seq Number         │
| └─ Received Sent Time          │
├────────────────────────────────┤
```
