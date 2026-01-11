# RapidFeed Kubernetes Deployment (Windows)

## Prerequisites

- Docker Desktop (with Kubernetes enabled)
- kubectl installed
- PowerShell or Command Prompt

## Structure

```
infra/
├── rapidfeed.yaml      # RapidFeed engine (1 pod)
├── prometheus.yaml     # Prometheus + alerts (1 pod)
├── grafana.yaml        # Grafana dashboard (1 pod)
└── kustomization.yaml  # Combines all above
```

## Quick Start

### 1. Build Docker Image

```powershell
docker build -t rapidfeed:latest -f infra/Dockerfile .
```

### 2. Deploy All (3 Pods)

```powershell
kubectl apply -k infra/
```

Or deploy individually:
```powershell
kubectl apply -f infra/rapidfeed.yaml
kubectl apply -f infra/prometheus.yaml
kubectl apply -f infra/grafana.yaml
```

### 3. Check Status

```powershell
kubectl get pods
kubectl get svc
```

### 4. Access Services

Open in browser:

- **RapidFeed**: http://localhost:30080/metrics
- **Prometheus**: http://localhost:30090 (alerts at http://localhost:30090/alerts)
- **Grafana**: http://localhost:30300 (login: admin/admin)

### 5. View Logs

```powershell
# RapidFeed logs
kubectl logs -l app=rapidfeed -f

# Prometheus logs
kubectl logs -l app=prometheus -f

# Grafana logs
kubectl logs -l app=grafana -f
```

### 6. Clean Up

```powershell
kubectl delete -k infra/
```

## Architecture

- **RapidFeed**: 1 pod, 2 CPU, 1GB RAM
- **Prometheus**: 1 pod, scrapes every 1s, 6 alerts
- **Grafana**: 1 pod for visualization
- **Total**: 3 pods running

