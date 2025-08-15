FROM node:20-alpine

WORKDIR /app

# Устанавливаем зависимости
COPY package.json ./
RUN npm install --omit=dev

# Копируем исходники
COPY src ./src
COPY public ./public

ENV PORT=8080
EXPOSE 8080

CMD ["npm", "start"]
