# My Own Redis

I built a Redis server which can receive a message from a client and send a message back

---

## Usage

### 1. Generate executable files by make all
```
make all
```

### 2. Start the server for receiving messages 
```
./server
```

### 3. Start the client request with the command formats (get, set, del)

#### get the value from key (something)
```
./client get something
```

#### set the value (1) for key (something)
```
./client set something 1
```

#### delele the key (something) from the database
```
./client del something
```
