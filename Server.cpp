//	В разделяемую память упаковываем имена всех процессов
//	С помощью pipe-ов передаём название его почтового ящика
//	Передаём текстовые сообщения с помощью почтовых ящиков

//	Убрали предупреждения по поводу безопасных операций
#define _CRT_SECURE_NO_WARNINGS 1

#include <locale.h>
#include <tchar.h>
#include <Windows.h>
#include <stdio.h>
#include <atlstr.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <fstream>

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
void compressMessage(const char* data, int data_len, int message_type, char* &message, int* message_len) {
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
void extractMessage(const char* message, int message_len, char* &data, int* data_len, int* message_type) {
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
void compressInfoMessage(const char* data, int data_len, char* &info_message, int* message_len) {
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

string getError() {
	string result;
	DWORD error_message_id = GetLastError();
	if (!error_message_id) {
		result = "";
	} else {
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
void stringToTCHAR(const string& str, TCHAR* &tchar, unsigned int *len) {
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

///
/// \brief Печатает список всех имён процессов
/// \param[in] names_list Список имён
/// 
void printNames(const vector<string>& names_list) {
	cout << "Список имён пользователей:\n";
	for (const string& name : names_list) {
		cout << name << endl;
	}
}

///
/// \brief Закрытие всех процессов
/// \param[in] pi_list Список информаций по процессам
/// 
void closeAllProcesses(const vector<PROCESS_INFORMATION>& pi_list) {
	for (const PROCESS_INFORMATION& pi : pi_list) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
}

///
/// \brief Ожидание завершения всех процессов
/// \param[in] pi_list Список информаций по процессам
/// 
void waitAllProcess(const vector<PROCESS_INFORMATION>& pi_list) {
	for (const PROCESS_INFORMATION& pi : pi_list) {
		WaitForSingleObject(pi.hProcess, INFINITE);
	}
}

///
/// \brief Закрытие всех заголовочных почтовых ящиков
/// \param[in] handle_list Список заголовков почтовых ящиков
/// 
void closeAllHandlers(const vector<HANDLE>& handle_list) {
	for (auto handle : handle_list) {
		CloseHandle(handle);
	}
}

///
/// \brief Чтение из pipe-а
/// \param[in] pipe_handle Handle pipe-а
/// \param[out] data Извлеченные данные
/// \return Факт удачи считывания или нет
/// \note
/// Если не удалось считать данные, то вернётся пустая строка
/// 
bool readFrom(const HANDLE& handle, char* &data, int *data_len, const string& error_message) {
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

void closeAll(const HANDLE& shared_memory_handle, const LPCTSTR& shared_memory_buf, const HANDLE& server_mailbox_handle, const vector<HANDLE>& mailbox_files_list, const vector<HANDLE>& pipe_list, const vector<PROCESS_INFORMATION>& process_list) {
	closeAllHandlers(pipe_list);
	closeAllHandlers(mailbox_files_list);
	closeAllProcesses(process_list);
	UnmapViewOfFile(shared_memory_buf);
	CloseHandle(shared_memory_handle);
	CloseHandle(server_mailbox_handle);
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
	for (const pair<string, int>& user : users) {
		cout << "(" << (user.second == 0 ? "Не в сети" : "В сети") << ")" << user.first << endl;
	}
}

void cyclePrintUsers(const LPCTSTR& shared_memory_buf, unsigned int number_clients) {
	int counter{ 1 };
	while (counter) {
		cout << "\nПечать состояний клиентов:\n";
		counter = 0;
		vector<pair<string, int>> users = getUsersStates(shared_memory_buf, number_clients);
		printUsersStates(users);
		for (auto i{ 0 }; i < number_clients; i++) {
			if (users[i].second) {
				counter++;
			}
		}
		if (counter) {
			Sleep(microseconds_wait_default * 5);
		}
	}
}

///
/// \brief Упаковка сообщения с контентом
/// 
void compressContentMessage(int sender_id, const vector<int>& users_id, const char* data, int data_len, char*& content_message, int* message_len) {
	//	[длина данных][номер пользователя, который отправил сообщение][скольким пользователям][номер пользователя]...[номер пользователя][длина сообщения][сообщение]
	*message_len = sizeof(int) + sizeof(int) + sizeof(int) + sizeof(int) * users_id.size() + sizeof(int) + data_len;
	content_message = new char[*message_len];
	memset(content_message, 0, *message_len);
	memcpy(content_message, message_len, sizeof(int));
	memcpy(content_message, &sender_id, sizeof(int));
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
void extractContentMessage(const char* content_message, int message_len, int* sender_id, vector<int>& users_id, char*& data, int* data_len) {
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
		}
		else {
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

void printBUF(const char* buf, int len) {
	for (int i{ 0 }; i < len; i++) {
		cout << int(buf[i]) << '\t';
	}
	cout << endl;
}

void cycleCheckMessages(const HANDLE& mailbox_handle, const vector<HANDLE>& users_mailbox_handlers, const vector<pair<string, int>>& users) {
	char* message_buf = new char[PIPE_BUFF_SIZE],
		* content_buf,
		* data_buf;
	int readen_len,
		len,
		message_type;
	//	Максимальное время жизни сообщения
	int wait_counter = 100;
	vector<pair<string, int>> wait;
	memset(message_buf, 0, PIPE_BUFF_SIZE);
	while (!flag_stop_work) {
		DWORD size_nex_message,
			number_messages;
		GetMailslotInfo(
			mailbox_handle,
			NULL,
			&size_nex_message,
			&number_messages,
			NULL);
		string message;
		if (number_messages > 0) {
			if (readFrom(
				mailbox_handle,
				message_buf,
				&readen_len,
				"Нет сообщений на чтение")) {
				cout << "Получили сообщение длинной " << readen_len << " байт\n";
				extractMessage(message_buf, readen_len, content_buf, &len, &message_type);
				printBUF(content_buf, len);
				if (message_type == 1) {	//	Обработка отправки сообщений пользователям
					int sender_id;
					vector<int> users_id;
					extractContentMessage(content_buf, len, &sender_id, users_id, data_buf, &len);
					printBUF(data_buf, len);
					delete[] content_buf;
					cout << "Сообщение было получено от пользователя <" << getUserName(users, sender_id) << ">\n";
					for (int user_id : users_id) {
						if (user_id >= users.size()) {
							cout << "Не возможно переслать сообщение не существующему клиенту!\n";
						} else {
							cout << "Отправка сообщения пользователю <" << getUserName(users, user_id) << ">\n";
							if (writeTo(users_mailbox_handlers[user_id], message_buf, readen_len, "Ошибка пересылки сообщения пользователю")) {
								cout << "Удачно переслали сообщение пользователю " << getUserName(users, user_id) << "\n";
								message = string(data_buf, len);
								message = "Пользователь <" + getUserName(users, user_id) + "> получил сообщение <" + message + "> от пользователя <" + getUserName(users, sender_id) + ">";
								wait.push_back(pair<string, int>{message, 0});
							} else {
								printError("Не удалось отправить сообщение клиенту <" + getUserName(users, user_id) + ">");
								cout << "Повторная отправка сообщения...\n";
								if (writeTo(users_mailbox_handlers[user_id], message_buf, readen_len, "Ошибка пересылки сообщения пользователю")) {
									cout << "Удачно переслали сообщение пользователю " << getUserName(users, user_id) << "\n";
									message = string(data_buf, len);
									message = "Пользователь <" + getUserName(users, user_id) + "> получил сообщение <" + message + "> от пользователя <" + getUserName(users, sender_id) + ">";
									wait.push_back(pair<string, int>{message, 0});
								}
								else {
									printError("Не удалось отправить сообщение клиенту <" + getUserName(users, user_id) + ">");
								}
							}
						}
					}
					delete[] data_buf;
				} else {
					extractInfoMessage(content_buf, len, data_buf, &len);
					delete[] content_buf;
					message = string(data_buf, len);
					cout << "Получили ответ от клиента <" << message << ">\n";
					for (int i{ 0 }; i < wait.size(); i++) {
						if (wait[i].first == message) {
							wait.erase(wait.begin() + i);
						}
					}
				}
				memset(message_buf, 0, readen_len);
			}
		}
		else {
			Sleep(microseconds_wait_default);
		}
		if (wait.size()) {
			cout << "Ожидается " << wait.size() << " ответов от клиентов\n";
		}
		for (int i{ 0 }; i < wait.size(); i++) {
			wait[i].second++;
			if (wait[i].second == wait_counter) {	//	Если пакет живёт слишком долго, то удаляем его
				wait.erase(wait.begin() + i);
			}
		}
	}
}

int main(int argc, char* argv[]) {

	//	Установили русскую кодировку
	setlocale(LC_ALL, "rus");

	string message = "My message";
	TCHAR* pipe_buf;
	char* input_buf,
		* message_buf,
		* data_buf;
	int tmp_len,
		message_type;

	if (argc < 2) {
		printError("Не передали путь к файлу клиента формата exe!", false);
		return 1;
	}

	//	Путь к исполняемому клиенту
	string client_program_path(argv[1]);

	//	Поток данных для проверки сущестования пути
	ifstream client_program_file(client_program_path.data());
	if (client_program_file.bad()) {
		printError("Ошибка открытия файла клиента");
		return 2;
	}
	client_program_file.close();

	//	Число клиентов
	unsigned int number_clients;

	cout << "Введите число клиентов: ";
	cin >> number_clients;
	if (number_clients < 0) {
		printError("Передано некорректное число клиентов", false);
		return 3;
	}

	//	Список имён клиентов
	vector<string> clients_names{};
	//	Счётчик
	int counter{ 0 };
	//	Переданное имя
	string name;
	getline(cin, name);
	while (counter != number_clients) {
		cout << "Введите название клиента:";
		getline(cin, name);
		if (name.size() >= max_name_len) {
			printError("Длина введённого имени больше макисмального <" + to_string(max_name_len) + " байт>!", false);
		} else if (find(clients_names.begin(), clients_names.end(), name) != clients_names.end()) {
			printError("Вы ввели существующее имя клиента!", false);
			printNames(clients_names);
		} else {
			clients_names.push_back(name);
			counter++;
		}
	}

	vector<STARTUPINFO> si_list{};
	vector<PROCESS_INFORMATION> pi_list{};

	for (auto i{ 0 }; i < number_clients; i++) {
		si_list.push_back(STARTUPINFO{});
		memset(&si_list[i], 0, sizeof(si_list[i]));
		si_list[i].cb = sizeof(si_list[i]);
		pi_list.push_back(PROCESS_INFORMATION{});
		memset(&pi_list[i], 0, sizeof(pi_list[i]));
	}

	string pipe_path = "\\\\.\\pipe\\chat\\";
	string mailbox_path = "\\\\.\\mailslot\\chat\\";
	string shared_memory_name = "Local\\Chat";

	TCHAR* tmp;
	unsigned int len;
	stringToTCHAR(shared_memory_name, tmp, &len);

	HANDLE shared_memory_handle = CreateFileMapping(
		INVALID_HANDLE_VALUE,
		NULL,
		PAGE_READWRITE,
		0,
		sizeof(int) + number_clients * (max_name_len + sizeof(int) * 2),
		tmp
	);
	delete[] tmp;
	if (shared_memory_handle == NULL){
		printError("Не удалось создать разделяемую память");
		return  4;
	}
	cout << "Создали разделяемую память <" << shared_memory_name << ">\n";
	LPCTSTR shared_memory_buf = (LPTSTR)MapViewOfFile(
		shared_memory_handle,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		number_clients * (max_name_len + sizeof(int) * 2) + sizeof(int)
	);
	if (shared_memory_buf == NULL) {
		printError("Не удалось получить доступ к разделяемой памяти");
		CloseHandle(shared_memory_handle);
		return 5;
	}
	cout << "Получили доступ к разделяемой памяти!\n";
	//	Заполняем разделяемую память
	//	Число пользователей
	memcpy((PVOID)shared_memory_buf, &number_clients, sizeof(int));
	int flag = 0;
	for (auto i{ 0 }; i < number_clients; i++) {
		//	Статус пользователя
		//	0	-	выключен
		//	1	-	работает
		memcpy((PVOID)(shared_memory_buf + sizeof(int) + (max_name_len + sizeof(int) * 2) * i), &flag, sizeof(int));
		int len = clients_names[i].size();
		memcpy((PVOID)(shared_memory_buf + sizeof(int) + (max_name_len + sizeof(int) * 2) * i + sizeof(int)), &len, sizeof(int));
		//	Имя клиента
		memcpy((PVOID)(shared_memory_buf + sizeof(int) + (max_name_len + sizeof(int) * 2) * i + sizeof(int) * 2), clients_names[i].data(), clients_names[i].size());
	}
	cout << "Заполнили разделяемую память\n";

	vector<HANDLE>	pipes_handlers{},
					mailboxes_files_handlers{};

	string server_mailbox_name = "\\\\.\\mailslot\\chat_server";
	stringToTCHAR(server_mailbox_name, tmp, &len);
	HANDLE server_mailbox_handler = CreateMailslot(
		tmp,
		0,
		MAILSLOT_WAIT_FOREVER,
		NULL
	);
	if (server_mailbox_handler == INVALID_HANDLE_VALUE) {
		printError("Ошибка создания почтового ящика сервера");
		closeAll(shared_memory_handle, shared_memory_buf, NULL, mailboxes_files_handlers, pipes_handlers,  pi_list);
		return 6;
	}

	vector<pair<string, int>> users = getUsersStates(shared_memory_buf, number_clients);
	printUsersStates(users);
	
	counter = 0;
	//	Создаём клиентов
	cout << "\nПорождение клиентов:\n";
	while (counter != number_clients) {
		cout << endl;
		unsigned int len;

		name.clear();
		name = pipe_path +deleteBackspaces(clients_names[counter]);
		TCHAR* current_pipe_path;
		stringToTCHAR(name, current_pipe_path, &len);
		cout << "Создаём pipe <";
		wcout << current_pipe_path;
		cout << "> для клиента <" << clients_names[counter] << ">\n";
		HANDLE current_pipe_handler = CreateNamedPipe(
			current_pipe_path,
			PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			PIPE_BUFF_SIZE,
			PIPE_BUFF_SIZE,
			0,
			NULL
		);
		if (current_pipe_handler == INVALID_HANDLE_VALUE) {
			printError("Ошибка создания pipe-а для клиента <" + clients_names[counter] + ">");
			closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
			return 5;
		} else {
			pipes_handlers.push_back(current_pipe_handler);
		}
		delete[] current_pipe_path;

		cout << "Создаём дочерний процесс для клиента " + clients_names[counter] + "\n";
		name = client_program_path + " \"" + name + "\"";
		TCHAR* client_command_start;
		stringToTCHAR(name, client_command_start, &len);
		//	Создаём дочерний процесс (клиента)
		if (!CreateProcess(
			NULL,
			client_command_start,
			NULL,
			NULL,
			FALSE,
			CREATE_NEW_CONSOLE,
			NULL,
			NULL,
			&si_list[counter],
			&pi_list[counter]
		)) {
			printError("Ошибка создания дочернего процесса");
			delete[] client_command_start;
			closeAllProcesses(pi_list);
			return 5;
		} else {
			delete[] client_command_start;
			//	Отправка имени разделяемой памяти
			name = shared_memory_name;
			char* buf,
				* message_buf,
				* input_buf;
			int len,
				message_type{ 0 };
			compressInfoMessage(name.data(), name.size(), buf, &len);
			compressMessage(buf, len, message_type, message_buf, &len);
			delete[] buf;
			if (writeTo(current_pipe_handler, message_buf, len, "Ошибка отправки сообщения в pipe")) {
				cout << "Отправили клиенту <" << clients_names[counter] << "> имя разделяемой памяти <" << name << ">\n";
			}
			else {
				printError("Не удалось отправить клиенту <" + clients_names[counter] + "> его имя...", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 6;
			}
			delete[] message_buf;

			if (readFrom(current_pipe_handler, input_buf, &len, "Ошибка чтения из pipe")) {
				extractMessage(input_buf, len, message_buf, &len, &message_type);
				delete[] input_buf;
				if (message_type == 0) {
					extractInfoMessage(message_buf, len, buf, &len);
					delete[] message_buf;
					name.clear();
					name = string(buf, len);
					delete[] buf;
					if (name != "Get memory") {
						printError("Информационное сообщение содержит не тот ответ <" + name + ">", false);
						closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
						return 7;
					}
				} else {
					printError("Получили не информационное сообщение", false);
					closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
					return 7;
				}
				cout << "Клиент <" << clients_names[counter] << "> получил название разделяемой памяти!\n";
			}
			else {
				printError("Клиент <" + clients_names[counter] + "> не получил своё имя!\n", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 7;
			}

			//	Отправка имени почтового ящика
			name.clear();
			name = mailbox_path + deleteBackspaces(clients_names[counter]);
			compressInfoMessage(name.data(), name.size(), buf, &len);
			compressMessage(buf, len, message_type, message_buf, &len);
			delete[] buf;
			if (writeTo(current_pipe_handler, message_buf, len, "Ошибка отправки сообщения в pipe")) {
				cout << "Отправили клиенту <" << clients_names[counter] << "> его название почтового ящика <" << name << ">\n";
			} else {
				printError("Не удалось отправить клиенту <" + clients_names[counter] + "> название его почтового ящика...", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 8;
			}
			delete[] message_buf;

			HANDLE current_mailbox_file_handler = CreateFileA(
				name.c_str(),
				GENERIC_ALL,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				NULL
			);
			if (current_mailbox_file_handler == INVALID_HANDLE_VALUE) {
				printError("Ошибка получения доступа к почтовому ящику <" + name + ">");
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers, pi_list);
				return 5;
			}
			else {
				cout << "Удачно создали файл доступа к почтовому ящику <" << name << "> клиента <" << clients_names[counter] << ">\n";
				mailboxes_files_handlers.push_back(current_mailbox_file_handler);
			}

			if (readFrom(current_pipe_handler, input_buf, &len, "Ошибка чтения из pipe")) {
				extractMessage(input_buf, len, message_buf, &len, &message_type);
				delete[] input_buf;
				if (message_type == 0) {
					extractInfoMessage(message_buf, len, buf, &len);
					delete[] message_buf;
					name.clear();
					name = string(buf, len);
					delete[] buf;
					if (name != "Get mailbox") {
						printError("Информационное сообщение содержит не тот ответ <" + name + ">", false);
						closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
						return 7;
					}
				}
				else {
					printError("Получили не информационное сообщение", false);
					closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
					return 7;
				}
				cout << "Клиент <" << clients_names[counter] << "> получил название почтового ящика!\n";
			} else {
				printError("Клиент <" + clients_names[counter] + "> не получил название почтового ящика!\n", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 9;
			}
			//	Отправка имени почтового ящика сервера
			name.clear();
			name = server_mailbox_name;
			compressInfoMessage(name.data(), name.size(), buf, &len);
			compressMessage(buf, len, message_type, message_buf, &len);
			delete[] buf;
			if (writeTo(current_pipe_handler, message_buf, len, "Ошибка записи сообщения в pipe")) {
				cout << "Отправили клиенту <" << clients_names[counter] << "> название почтового ящика  сервера <" << name << ">\n";
			}
			else {
				printError("Не удалось отправить клиенту <" + clients_names[counter] + "> название почтового ящика сервера...", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 8;
			}
			delete[] message_buf;

			name.clear();
			if (readFrom(current_pipe_handler, input_buf, &len, "Ошибка чтения из pipe")) {
				extractMessage(input_buf, len, message_buf, &len, &message_type);
				delete[] input_buf;
				if (message_type == 0) {
					extractInfoMessage(message_buf, len, buf, &len);
					delete[] message_buf;
					name.clear();
					name = string(buf, len);
					delete[] buf;
					if (name != "Get server_mailbox") {
						printError("Информационное сообщение содержит не тот ответ <" + name + ">", false);
						closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
						return 7;
					}
				}
				else {
					printError("Получили не информационное сообщение", false);
					closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
					return 7;
				}
				cout << "Клиент <" << clients_names[counter] << "> получил название почтового ящика сервера!\n";
			}
			else {
				printError("Клиент <" + clients_names[counter] + "> не получил название почтового ящика сервера!\n", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 9;
			}

			//	Отправка имени клиента
			name = clients_names[counter];
			compressInfoMessage(name.data(), name.size(), buf, &len);
			compressMessage(buf, len, message_type, message_buf, &len);
			delete[] buf;
			if (writeTo(current_pipe_handler, message_buf, len, "Ошибка записи сообщения в pipe")) {
				cout << "Отправили клиенту <" << clients_names[counter] << "> его имя <" << name << ">\n";
			}
			else {
				printError("Не удалось отправить клиенту <" + clients_names[counter] + "> его имя...", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 10;
			}
			delete[] message_buf;

			name.clear();
			if (readFrom(current_pipe_handler, input_buf, &len, "Ошибка чтения из pipe")) {
				extractMessage(input_buf, len, message_buf, &len, &message_type);
				delete[] input_buf;
				if (message_type == 0) {
					extractInfoMessage(message_buf, len, buf, &len);
					delete[] message_buf;
					name.clear();
					name = string(buf, len);
					delete[] buf;
					if (name != "Get name") {
						printError("Информационное сообщение содержит не тот ответ <" + name + ">", false);
						closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
						return 7;
					}
				}
				else {
					printError("Получили не информационное сообщение", false);
					closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
					return 7;
				}
				cout << "Клиент <" << clients_names[counter] << "> получил своё имя!\n";
			}
			else {
				printError("Клиент <" + clients_names[counter] + "> не получил своё имя!\n", false);
				closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
				return 11;
			}
		}
		counter++;
	}

	Sleep(microseconds_wait_default * 3);
	users = getUsersStates(shared_memory_buf, number_clients);
	printUsersStates(users);

	thread cycle_check_users(cyclePrintUsers, shared_memory_buf, number_clients);
	thread cycle_check_messages(cycleCheckMessages, server_mailbox_handler, mailboxes_files_handlers, users);
	thread wait_processes(waitAllProcess, pi_list);
	cycle_check_users.join();
	flag_stop_work = true;
	cycle_check_messages.join();
	//	Ожидание завершения работы всех процессов
	wait_processes.join();
	closeAll(shared_memory_handle, shared_memory_buf, server_mailbox_handler, mailboxes_files_handlers, pipes_handlers,  pi_list);
	return 0;
}
