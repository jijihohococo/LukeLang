# LukeLang API

HTTP API scaffold created with `mimo init --template api`.

## Quick start

```bash
mimo run
curl http://localhost:8080/health
curl http://localhost:8080/user/1?tag=dev
```

## Routes

| Method | Path | Description |
| --- | --- | --- |
| GET | `/health` | Liveness check |
| GET | `/ok` | Simple OK response |
| GET | `/user/:id` | User lookup (`?tag=` query param) |
| GET | `/me` | Session user (cookie `luke_sid`) |
| POST | `/login` | JSON body `{"user":"name"}` |

## Next steps

- Edit `main.lk` to add routes
- `mimo forge greeter` to add a registry package
- See [lukelang.org/learn](https://lukelang.org/learn/) for tutorials
