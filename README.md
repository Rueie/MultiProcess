# Описание
В данной работе показаны механизмы взаимодействия процессов в WINDOWS. Использованы pipe-ы, shared memory и mailbox-ы. <br>
Они реализованы на примере чата, где у нас имеется сервер и клиенты. <br>
# Сервер
Запускается с одним параметром, а именно - путём к собранному .exe клиента. <br>
Далее мы водим число клиентов для чата и их имена. После этого сервер создаёт разделяемую память, в которую записывает состояние клиентов и их имена. <br>
После сервер создаёт pipe-ы для каждого клиента, по которым передаёт им их данные для инициализации (имя, название его почтового ящика, название почтового ящика сервера, название разделяемой памяти). <br>
После создаётся клиент, которому эти данные и передаются (клиент отвечает, получил ли он исправно данные или нет).
После инициализации всех клиентов начинается проверка их статуса в shared memory и ожидание сообщений.

Во время работы сервер проверяет, жив ли клиент и пересылает нужному сообщение. Также он должен дождаться ответа от клента, (в течение 100 тактов, 
# Клиент
Послее инициализации клиент обновляет свой статус в shared memory и может:
* посмотерть кто он (имя)
* посмотреть состояния других клиентов, в сети они или нет (клиенты напрямую обращаются к разделяемой памяти)
* написать сообщение 1 или нескольким пользователям (себе тоже можно)

После выключения или аварийной остановки клиент также должен обновить свой статус в shared memory. <br>
После получения сообщения клиент должен отправить серверу подтверждение того, что получил сообщение.
# Пример запуска
```bash
# Запускаем из папки двух проектов:
Server\Debug\Server.exe Client\Debug\Client.exe 
```
