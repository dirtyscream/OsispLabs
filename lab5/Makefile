# Имя компилятора
CC = gcc
# Флаги компиляции (общие)
# -pthread нужен для работы с потоками (pthreads) и POSIX семафорами/условными переменными
# Добавлены флаги из вашего лога компиляции для большей строгости
CFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-align -Wconversion -Wsign-conversion -Wnull-dereference -g3 -O0 -std=c11 -pthread

# Целевые исполняемые файлы
TARGET1 = lab5_1
TARGET2 = lab5_2

# Исходные файлы
SRC1 = task1.c
SRC2 = task2.c

# Библиотеки (если нужны дополнительные, например, для math -lm)
LIBS = -lm # -lm может быть не нужен, но оставлен на всякий случай

# Правило по умолчанию: собрать обе задачи
all: $(TARGET1) $(TARGET2)

# Правило для сборки первой задачи (POSIX семафоры)
$(TARGET1): $(SRC1)
	$(CC) $(CFLAGS) -o $(TARGET1) $(SRC1) $(LIBS)
	@echo "Built $(TARGET1) from $(SRC1)"

# Правило для сборки второй задачи (условные переменные)
$(TARGET2): $(SRC2)
	$(CC) $(CFLAGS) -o $(TARGET2) $(SRC2) $(LIBS)
	@echo "Built $(TARGET2) from $(SRC2)"

# Правило для очистки сгенерированных файлов
clean:
	rm -f $(TARGET1) $(TARGET2) *.o # Удаляем исполняемые файлы и объектные файлы
	@echo "Cleaned build files."

# Правило для запуска первой задачи
run1: $(TARGET1)
	./$(TARGET1)

# Правило для запуска второй задачи
run2: $(TARGET2)
	./$(TARGET2)

# Фальшивые цели (phony targets) - цели, которые не являются файлами
.PHONY: all clean run1 run2