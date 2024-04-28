#include <locale.h>
#include <tchar.h>
#include <Windows.h>
#include <stdio.h>
#include <atlstr.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>

using namespace std;

//	Максимальный размер буфера pipe-ов
unsigned int PIPE_BUFF_SIZE = 2024;
int microseconds_wait_default = 1000;
int max_number_try_to_read = 3;
int max_name_len = 128;

bool flag_stop_work{ false };

void charToTCHAR(const char* data, int data_len, TCHAR*& result, int* result_len) {
	result = new TCHAR[data_len];
	memset(result, 0, data_len);
	memcpy(result, data, data_len);
	*result_len = data_len;
}

void TCHARtoChar(const TCHAR* data, int data_len, char*& result, int* result_len) {

	result = new char[data_len];
	memset(result, 0, data_len);
	memcpy(result, data, data_len);
	*result_len = data_len;
}

///
/// \brief Упаковка сообщения для отправки
/// \note Структура сообщения
/// [длина сообщения][тип сообщения][данные сообщения]
/// 
void compressMessage(const char* data, int data_len, int message_type, char*& message, int* message_len) {
	*message_len = data_len + sizeof(int) * 2;
	message = new char[*message_len];
	memset(message, 0, data_len + sizeof(int));
	memcpy(message, message_len, sizeof(int));
	memcpy(message + sizeof(int), &message_type, sizeof(int));
	memcpy(message + sizeof(int) * 2, data, data_len);
}

///
/// \brief Извлечение присланного сообщения
/// 
void extractMessage(const char* message, int message_len, char*& data, int* data_len, int* message_type) {
	memcpy(message_type, message + sizeof(int), sizeof(int));
	*data_len = message_len - sizeof(int) * 2;
	data = new char[*data_len];
	memset(data, 0, *data_len);
	memcpy(data, message + sizeof(int) * 2, *data_len);
}

///
/// \brief Упаковка информационного сообщения
/// \note Структура сообщения
/// [длина информации][сама информация]
/// 
void compressInfoMessage(const char* data, int data_len, char*& info_message, int* message_len) {
	*message_len = data_len + sizeof(int);
	info_message = new char[*message_len];
	memset(info_message, 0, *message_len);
	memcpy(info_message, &data_len, sizeof(int));
	memcpy(info_message + sizeof(int), data, data_len);
}

///
/// \brief Извлечение информационного сообщения
/// 
void extractInfoMessage(const char* info_message, int message_len, char*& data, int* data_len) {
	memcpy(data_len, info_message, sizeof(int));
	data = new char[*data_len];
	memset(data, 0, *data_len);
	memcpy(data, info_message + sizeof(int), *data_len);
}

///
/// \brief Упаковка сообщения с контентом
/// 
void compressContentMessage(int sender_id, const vector<int>& users_id, const char* data, int data_len, char* &content_message, int* message_len) {
	//	[длина данных][номер пользователя, который отправил сообщение][скольким пользователям][номер пользователя]...[номер пользователя][длина сообщения][сообщение]
	*message_len = sizeof(int) + sizeof(int) + sizeof(int) + sizeof(int) * users_id.size() + sizeof(int) + data_len;
	content_message = new char[*message_len];
	memset(content_message, 0, *message_len);
	memcpy(content_message, message_len, sizeof(int));
	memcpy(content_message + sizeof(int), &sender_id, sizeof(int));
	int number_users = users_id.size();
	memcpy(content_message + sizeof(int) * 2, &number_users, sizeof(int));
	int user_id;
	for (int i{ 0 }; i < number_users; i++) {
		user_id = users_id[i];
		memcpy(content_message + sizeof(int) * 3 + sizeof(int) * i, &user_id, sizeof(int));
	}
	memcpy(content_message + sizeof(int) * 3 + sizeof(int) * number_users, &data_len, sizeof(int));
	memcpy(content_message + sizeof(int) * 3 + sizeof(int) * number_users + sizeof(int), data, data_len);
}

///
/// \brief Извлекает контент и данные о нём
/// 
void extractContentMessage(const char* content_message, int message_len, int* sender_id, vector<int>& users_id, char* &data, int *data_len) {
	int number_users;
	memcpy(sender_id, content_message + sizeof(int), sizeof(int));
	memcpy(&number_users, content_message + sizeof(int) * 2, sizeof(int));
	users_id.clear();
	int user_id;
	for (int i{ 0 }; i < number_users; i++) {
		memcpy(&user_id, content_message + sizeof(int) * 3 + sizeof(int) * i, sizeof(int));
		users_id.push_back(user_id);
	}
	memcpy(data_len, content_message + sizeof(int) * 3 + sizeof(int) * number_users, sizeof(int));
	data = new char[*data_len];
	memset(data, 0, *data_len);
	memcpy(data, content_message + sizeof(int) * 3 + sizeof(int) * number_users + sizeof(int), *data_len);
}

///
/// \brief Удаляет проблеы в строке и преобразует все символы в нижний регистр
/// \param[in] str Входная строка
/// \return Результат
/// 
string deleteBackspaces(const string& str) {
	string result;
	for (auto simv : str) {
		if (simv != ' ') {
			result += tolower(simv);
		}
	}
	return result;
}

///
/// \brief Преобразование строки в tchar
/// \param[in] str Строка
/// \param[out] tchar TCHAR строка
/// \param[out] len Длина TCHAR строки
/// 
void stringToTCHAR(const string& str, TCHAR*& tchar, int* len) {
	tchar = new TCHAR[str.size() + 1];
	memset(tchar, 0, str.size() + 1);
	copy(str.begin(), str.end(), tchar);
	tchar[str.size()] = 0;
	*len = str.size() + 1;
}

string TCHARtoString(TCHAR* tchar) {
	wstring tmp(tchar);
	string result(tmp.begin(), tmp.end());
	return result;
}

string getError() {
	string result;
	DWORD error_message_id = GetLastError();
	if (!error_message_id) {
		result = "";
	}
	else {
		LPSTR messageBuffer = nullptr;

		//Ask Win32 to give us the string version of that message ID.
		//The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
		size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, error_message_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

		//Copy the error message into a std::string.
		std::string message(messageBuffer, size);

		//Free the Win32's string's buffer.
		LocalFree(messageBuffer);

		result = move(message);
	}
	return result;
}

///
///	\brief Печатает ошибку и её содержание
///	\param[in] info Загаловок ошибки
///	\param[in] print_errno Флаг того, нужно ли печатать содержимое ошибки
///	\info
///	Печатается в формате [info]:[содержимое ошибки]
///
void printError(const string& info, bool print_errno = true) {
	cout << info;
	if (print_errno) {
		cout << ": " << getError();
	}
	cout << endl;
}

///
/// \brief Чтение из pipe-а
/// \param[in] pipe_handle Handle pipe-а
/// \param[out] data Извлеченные данные
/// \return Факт удачи считывания или нет
/// \note
/// Если не удалось считать данные, то вернётся пустая строка
/// 
bool readFrom(const HANDLE& handle, char*& data, int* data_len, const string& error_message) {
	int number_try_to_read{ 0 };
	TCHAR* input_buf = new TCHAR[PIPE_BUFF_SIZE];
	memset(input_buf, 0, PIPE_BUFF_SIZE * sizeof(TCHAR));
	DWORD input_len{ 0 };
	while (number_try_to_read != max_number_try_to_read) {
		if (ReadFile(
			handle,
			input_buf,
			PIPE_BUFF_SIZE,
			&input_len,
			NULL
		)) {
			TCHARtoChar(input_buf, input_len, data, data_len);
			break;
		}
		else {
			printError(error_message);
			number_try_to_read++;
			Sleep(microseconds_wait_default);
		}
	}
	delete[] input_buf;
	if (number_try_to_read != max_number_try_to_read) {
		*data_len = input_len;
		return true;
	}
	else {
		*data_len = 0;
		return false;
	}
}

///
/// \brief Отправляет данные по pipe-у
/// \param[in] pipe_handle Handle pipe-а
/// \param[in] data Отправялемая строка
/// \return Результат отправки сообщения
/// 
bool writeTo(const HANDLE& handle, const char* data, int data_len, const string& error_message) {
	TCHAR* outbuf;
	int len;
	charToTCHAR(data, data_len, outbuf, &len);
	DWORD len_write{ 0 };
	int number_try_to_read = 0;
	while (number_try_to_read != max_number_try_to_read) {
		if (WriteFile(
			handle,
			outbuf,
			len,
			&len_write,
			NULL
		)) {
			break;
		}
		else {
			printError(error_message);
			number_try_to_read++;
			Sleep(microseconds_wait_default);
		}
	}
	delete[] outbuf;
	if (number_try_to_read != max_number_try_to_read) {
		return true;
	}
	else {
		return false;
	}
}

///
/// \brief Извлекает из разделяемой памяти данные пользователей
/// \param[in] shared_memory_buf Указатель на разделяемую память
/// \param[in] number_clients Число пользователей
/// \return map-а пользователь - состояние
/// 
vector<pair<string, int>> getUsersStates(const LPCTSTR& shared_memory_buf, unsigned int number_clients) {
	vector<pair<string, int>> result;
	for (auto i{ 0 }; i < number_clients; i++) {
		string name;
		int status,
			len;
		memcpy(&status, (PVOID)(shared_memory_buf + sizeof(int) + (sizeof(int) * 2 + max_name_len) * i), sizeof(int));
		memcpy(&len, (PVOID)(shared_memory_buf + sizeof(int) + (sizeof(int) * 2 + max_name_len) * i + sizeof(int)), sizeof(int));
		name.resize(len);
		memcpy(&name[0], (PVOID)(shared_memory_buf + sizeof(int) + (sizeof(int) * 2 + max_name_len) * i + sizeof(int) * 2), len);
		result.push_back(pair<string, int>{name, status});
	}
	return result;
}

///
/// \brief Печатает список пользователей с их состоянимями 
/// \param[in] users Список пользователей с их состоянием
/// 
void printUsersStates(const vector<pair<string, int>>& users) {
	int counter{ 0 };
	for (const pair<string, int>& user : users) {
		cout << counter << ")(" << (user.second == 0 ? "Не в сети" : "В сети") << ")" << user.first << endl;
		counter++;
	}
}

///
/// \brief Осуществляет поиск номера пользователя в общей памяти
/// \param[in] users Список пользователей
/// \param[in] name Имя пользователя
/// \return Номер пользователя
/// \note
/// Возвращает -1, если такого пользователя нет
/// 
int getUserNumber(const vector<pair<string, int>>& users, const string& name) {
	int result{ 0 };
	for (const pair<string, int>& user : users) {
		if (user.first == name) {
			return result;
		} else {
			result++;
		}
	}
	return -1;
}

string getUserName(const vector<pair<string, int>>& users, int user_id) {
	string result = "";
	if (user_id >= 0 && user_id < users.size()) {
		result = users[user_id].first;
	}
	return result;
}

void updateUserStatus(const LPCTSTR& shared_memory_buf, const vector<pair<string, int>>& users, const string& user_name, int status) {
	int number = getUserNumber(users, user_name);
	if (number >= 0) {
		memcpy((PVOID)(shared_memory_buf + sizeof(int) + (max_name_len + sizeof(int) * 2) * number), &status, sizeof(int));
	}
}

vector<int> parseStringToNumbers(const string& str) {
	vector<int> result;
	string number{""};
	for (auto i{ 0 }; i < str.length(); i++) {
		if (str[i] != ' ') {
			number += str[i];
		} else {
			result.push_back(stoi(number));
			number.clear();
		}
	}
	if (number.length() > 0) {
		result.push_back(stoi(number));
	}
	return result;
}

void cycleCheckMessages(const HANDLE& mailbox_handle, const string& user_name, const vector<pair<string, int>>& users_id, const HANDLE& server_mailbox) {
	TCHAR* input_message = new TCHAR[PIPE_BUFF_SIZE];
	memset(input_message, 0, PIPE_BUFF_SIZE);
	DWORD readen_len{ 0 };
	char* message_buf,
		* content_buf,
		* data_buf;
	int len,
		message_type;
	DWORD size_nex_message,
		number_messages;
	while (!flag_stop_work) {
		GetMailslotInfo(
			mailbox_handle,
			NULL,
			&size_nex_message,
			&number_messages,
			NULL
		);
		if (number_messages > 0) {
			if (ReadFile(
				mailbox_handle,
				input_message,
				PIPE_BUFF_SIZE,
				&readen_len,
				NULL)) {
				TCHARtoChar(input_message, readen_len, message_buf, &len);
				memset(input_message, 0, readen_len);
				extractMessage(message_buf, len, content_buf, &len, &message_type);
				delete[] message_buf;
				if (message_type == 1) {
					vector<int> recieved_users_id;
					int sender_id;
					extractContentMessage(content_buf, len, &sender_id, recieved_users_id, data_buf, &len);
					delete[] content_buf;
					string message(data_buf, len);
					delete[] data_buf;
					cout << "\nПришло сообщение от пользователя <" << getUserName(users_id, sender_id) << ">: [" << message << "]\n";
					message = "Пользователь <" + user_name + "> получил сообщение <" + message + "> от пользователя <" + getUserName(users_id, sender_id) + ">";
					compressInfoMessage(message.data(), message.size(), data_buf, &len);
					compressMessage(data_buf, len, 0, message_buf, &len);
					delete[] data_buf;
					if (writeTo(
						server_mailbox,
						message_buf,
						len,
						"Ошибка отправки подтверждения получения сообщения на сервер")) {

					} else {
						printError("Не удалось отправить подтверждение получения сообщения на сервер");
					}
					delete[] message_buf;
				} else {
				}
			}
		} else {
			Sleep(microseconds_wait_default);
		}
	}
}

int main(int argc, char *argv[] ) {

	setlocale(LC_ALL, "rus");

	if (argc < 2) {
		cout << "Нет аргументов!\n";
		return 1;
	}
	cout << "Получили название pipe-a <" << argv[1] << ">\n";

	string	pipe_name(argv[1]),
			mailbox_name{},
			server_mailbox_name{},
			shared_memory_name{},
			current_name{};
	HANDLE pipe_handle = CreateFileA(
		pipe_name.c_str(),
		GENERIC_ALL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if (pipe_handle == INVALID_HANDLE_VALUE) {
		printError("Ошибка в подключении к pipe-у <" + pipe_name + ">\n");
		return 2;
	}
	char* buf,
		* message_buf,
		* input_buf;
	int len,
		message_type;
	string message;
	//	Ожидание имени разделяемой памяти
	if (readFrom(pipe_handle, input_buf, &len, "Ошибка чтения из pipe")) {
		extractMessage(input_buf, len, message_buf, &len, &message_type);
		delete[] input_buf;
		if (message_type != 0) {
			printError("Присланно не информационное сообщение", false);
			CloseHandle(pipe_handle);
			return 3;
		}
		extractInfoMessage(message_buf, len, buf, &len);
		delete[] message_buf;
		shared_memory_name = string(buf, len);
		delete[] buf;
		cout << "Получили название разделяемой памяти <" << shared_memory_name << ">\n";
	}
	else {
		cout << "Не удалось получить название разделяемой памяти от сервера\n";
		CloseHandle(pipe_handle);
		return 3;
	}
	message = "Get memory";
	compressInfoMessage(message.data(), message.size(), buf, &len);
	compressMessage(buf, len, 0, message_buf, &len);
	delete[] buf;
	if (writeTo(pipe_handle, message_buf, len, "Ошибка записи в pipe")) {
		cout << "Отправили серверу ответ, что получили имя клиента\n";
	}
	else {
		cout << "Не удалось отправить серверу ответ по поводу получения имени клиента\n";
		CloseHandle(pipe_handle);
		return 4;
	}
	delete[] message_buf;
	//	Ожидание названия почтового ящика
	if (readFrom(pipe_handle, input_buf, &len, "Ошибка чтения из pipe")) {
		extractMessage(input_buf, len, message_buf, &len, &message_type);
		delete[] input_buf;
		if (message_type != 0) {
			printError("Присланно не информационное сообщение", false);
			CloseHandle(pipe_handle);
			return 3;
		}
		extractInfoMessage(message_buf, len, buf, &len);
		delete[] message_buf;
		mailbox_name = string(buf, len);
		delete[] buf;
		cout << "Получили название почтового ящика <" << mailbox_name << ">\n";
	} else {
		cout << "Не удалось получить название почтового ящика\n";
		CloseHandle(pipe_handle);
		return 5;
	}

	TCHAR* tmp_buf;

	stringToTCHAR(mailbox_name, tmp_buf, &len);
	HANDLE current_mailbox_handle = CreateMailslotA(
		mailbox_name.c_str(),
		0,
		MAILSLOT_WAIT_FOREVER,
		NULL
	);
	if (current_mailbox_handle == INVALID_HANDLE_VALUE) {
		printError("Не удалось создать свой почтовый ящик");
		CloseHandle(pipe_handle);
		return 6;
	}
	cout << "Создали почтовый ящик <" << mailbox_name << ">\n";

	message = "Get mailbox";
	compressInfoMessage(message.data(), message.size(), buf, &len);
	compressMessage(buf, len, 0, message_buf, &len);
	delete[] buf;
	if (writeTo(pipe_handle, message_buf, len, "Ошибка записи в pipe")) {
		cout << "Отправили серверу ответ, что получили название почтового ящика\n";
	} else {
		cout << "Не удалось отправить серверу ответ по поводу получения названия почтового ящика\n";
		CloseHandle(pipe_handle);
		return 6;
	}
	delete[] message_buf;
	//	Ожидание названия почтового ящика сервера
	if (readFrom(pipe_handle, input_buf, &len, "Ошибка чтения из pipe")) {
		extractMessage(input_buf, len, message_buf, &len, &message_type);
		delete[] input_buf;
		if (message_type != 0) {
			printError("Присланно не информационное сообщение", false);
			CloseHandle(pipe_handle);
			return 3;
		}
		extractInfoMessage(message_buf, len, buf, &len);
		delete[] message_buf;
		server_mailbox_name = string(buf, len);
		delete[] buf;
		cout << "Получили название почтового ящика сервера <" << server_mailbox_name << ">\n";
	}
	else {
		cout << "Не удалось получить название почтового ящика сервера\n";
		CloseHandle(pipe_handle);
		return 5;
	}
	message = "Get server_mailbox";
	compressInfoMessage(message.data(), message.size(), buf, &len);
	compressMessage(buf, len, 0, message_buf, &len);
	delete[] buf;
	if (writeTo(pipe_handle, message_buf, len, "Ошибка записи в pipe")) {
		cout << "Отправили серверу ответ, что получили название почтового ящика сервера\n";
	}
	else {
		cout << "Не удалось отправить серверу ответ по поводу получения названия почтового ящика сервера\n";
		CloseHandle(pipe_handle);
		return 6;
	}
	delete[] message_buf;
	//	Ожидание имени клиента
	if (readFrom(pipe_handle, input_buf, &len, "Ошибка чтения из pipe")) {
		extractMessage(input_buf, len, message_buf, &len, &message_type);
		delete[] input_buf;
		if (message_type != 0) {
			printError("Присланно не информационное сообщение", false);
			CloseHandle(pipe_handle);
			return 3;
		}
		extractInfoMessage(message_buf, len, buf, &len);
		delete[] message_buf;
		current_name = string(buf, len);
		delete[] buf;
		cout << "Получили название клиента <" << current_name << ">\n";
	} else {
		cout << "Не удалось получить название клиента от сервера\n";
		CloseHandle(pipe_handle);
		return 7;
	}
	message = "Get name";
	compressInfoMessage(message.data(), message.size(), buf, &len);
	compressMessage(buf, len, 0, message_buf, &len);
	delete[] buf;
	if (writeTo(pipe_handle, message_buf, len, "Ошибка записи в pipe")) {
		cout << "Отправили серверу ответ, что получили имя клиента\n";
	}
	else {
		cout << "Не удалось отправить серверу ответ по поводу получения имени клиента\n";
		CloseHandle(pipe_handle);
		return 8;
	}
	delete[] message_buf;
	CloseHandle(pipe_handle);

	if (current_mailbox_handle == INVALID_HANDLE_VALUE) {
		printError("Не удалось открыть свой почтовый ящик");
		Sleep(microseconds_wait_default * 3);
		return 9;
	}
	cout << "Удачно открыли свой почтовый ящик\n";
	stringToTCHAR(server_mailbox_name, tmp_buf, &len);
	HANDLE server_mailbox_handle = CreateFile(
		tmp_buf,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	delete[] tmp_buf;
	if (server_mailbox_handle == INVALID_HANDLE_VALUE) {
		printError("Не удалось открыть почтовый ящик сервера");
		CloseHandle(current_mailbox_handle);
		Sleep(microseconds_wait_default * 3);
		return 10;
	}


	stringToTCHAR(shared_memory_name, tmp_buf, &len);
	HANDLE shared_memory_handle = OpenFileMapping(
		FILE_MAP_ALL_ACCESS,
		FALSE,
		tmp_buf
	);
	delete[] tmp_buf;
	if (shared_memory_handle == NULL) {
		printError("Ошибка открытия разделяемой памяти <" + shared_memory_name + ">");
		return 9;
	}
	cout << "Открыли разделяемую память <" << shared_memory_name << ">\n";
	LPCTSTR shared_memory_buf = (LPTSTR)MapViewOfFile(
		shared_memory_handle,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		sizeof(int)
	);
	if (shared_memory_buf == NULL) {
		printError("Не удалось получить доступ к разделяемой памяти");
		CloseHandle(shared_memory_handle);
		return 10;
	}
	int number_clients;
	memcpy(&number_clients, (PVOID)shared_memory_buf, sizeof(int));
	//	Извлекли число записей
	UnmapViewOfFile(shared_memory_buf);
	shared_memory_buf = (LPTSTR)MapViewOfFile(
		shared_memory_handle,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		number_clients * (max_name_len + sizeof(int) * 2) + sizeof(int)
	);
	if (shared_memory_buf == NULL) {
		printError("Ошибка получения доступа ко всей разделяемой памяти");
		Sleep(microseconds_wait_default * 3);
		CloseHandle(shared_memory_handle);
		return 11;
	}

	vector<pair<string, int>> users = getUsersStates(shared_memory_buf, number_clients);
	updateUserStatus(shared_memory_buf, users, current_name, 1);	//	ВОТ ТУТ ПРОБЛЕМА ДЛЯ ПОЛЬЗОВАТЕЛЕЙ с составным именем, удаление пробелов не решило проблемы!
	users = getUsersStates(shared_memory_buf, number_clients);

	thread cycle_check_mail_box(cycleCheckMessages, current_mailbox_handle, current_name, users, server_mailbox_handle);

	bool flag_stop{ false };
	//	ТУТ ЗАПУСКАЕМ ПОТОК НА ПРИЁМ СООБЩЕНИЙ
	while (true) {
		cout << "Меню:\n";
		cout << "0 - выход\n";
		cout << "1 - кто я\n";
		cout << "2 - посмотреть статус других клиентов\n";
		cout << "3 - отправить сообщение\n";
		cout << "Введите ваш выбор:";
		int choice;
		cin >> choice;
		switch (choice)
		{
		case 0:
			flag_stop = true;
			break;
		case 1:
			cout << "Я <" << current_name << ">\n";
			break;
		case 2:
			users = getUsersStates(shared_memory_buf, number_clients);
			printUsersStates(users);
			break;
		case 3: {
			cout << "Введите сообщение: ";
			message.clear();
			getline(cin, message);
			getline(cin, message);
			cout << "Введите номера пользователей, которым будет отправлены сообщения: ";
			string users_number_str{ "" };
			getline(cin, users_number_str);
			cout << "Ваше сообщение: " << message << endl;
			vector<int> users_numbers = parseStringToNumbers(users_number_str);
			cout << "Отправляется следующим пользователям: ";
			for (auto i{ 0 }; i < users_numbers.size(); i++) {
				string name = getUserName(users, users_numbers[i]);
				if (!name.empty()) {
					cout << name;
					if (i != users_numbers.size() - 1) {
						cout << ", ";
					} else {
						cout << ".\n";
					}
				}
			}
			cout << endl;
			cout << "Текущее имя: " << current_name << endl;
			cout << "Номер пользователя: " << getUserNumber(users, current_name) << endl;
			compressContentMessage(getUserNumber(users, current_name), users_numbers, message.data(), message.size(), buf, &len);
			compressMessage(buf, len, 1, message_buf, &len);
			delete[] buf;
			if (writeTo(server_mailbox_handle, message_buf, len, "Ошибка отправки сообщения на почтовый ящик сервера")) {
				cout << "Отправили сообщение на сервер, ожидаем ответы от получателей сообщений\n";
				//	Тут поток с ожиданием ответов
			} else {
				flag_stop_work = true;
				cycle_check_mail_box.join();
				printError("Не удалось отправить сообщение на сервер", false);
				updateUserStatus(shared_memory_buf, users, current_name, 0);
				CloseHandle(current_mailbox_handle);
				CloseHandle(server_mailbox_handle);
				UnmapViewOfFile(shared_memory_buf);
				CloseHandle(shared_memory_handle);
			}
			delete[] message_buf;
		}break;
		default:
			break;
		}
		if (flag_stop) {
			break;
		}
	}
	flag_stop_work = true;
	cycle_check_mail_box.join();
	updateUserStatus(shared_memory_buf, users, current_name, 0);
	CloseHandle(current_mailbox_handle);
	CloseHandle(server_mailbox_handle);
	UnmapViewOfFile(shared_memory_buf);
	CloseHandle(shared_memory_handle);

	return 0;
}