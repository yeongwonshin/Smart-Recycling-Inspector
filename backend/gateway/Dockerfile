FROM node:20-bookworm-slim
WORKDIR /app
COPY backend/gateway/package*.json ./backend/gateway/
RUN cd backend/gateway && npm install
COPY proto ./proto
COPY backend/gateway ./backend/gateway
WORKDIR /app/backend/gateway
EXPOSE 8080
CMD ["npm", "start"]
