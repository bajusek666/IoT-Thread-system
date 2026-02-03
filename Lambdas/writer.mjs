import { S3Client, PutObjectCommand } from '@aws-sdk/client-s3';

const s3 = new S3Client({});
const BUCKET_NAME = 'temperature-measurements-iot-system';

export const handler = async (event) => {
	try {
		let data;
		if (event.body) {
			// Dane przychodzą przez API Gateway (jako tekst)
			data =
				typeof event.body === 'string' ? JSON.parse(event.body) : event.body;
		} else {
			// Dane przychodzą bezpośrednio (np. z testu w konsoli)
			data = event;
		}
		const deviceId = data.deviceId || 'unknown';
		const timestamp = new Date().toISOString();
		const temperature = data.temperature;
		const fileName = `measurements/${deviceId}_${timestamp}.json`;

		const command = new PutObjectCommand({
			Bucket: BUCKET_NAME,
			Key: fileName,
			Body: JSON.stringify(data),
			ContentType: 'aplication/json',
		});

		await s3.send(command);

		return {
			statusCode: 200,
			body: JSON.stringify({
				message: `Zapisano: ${fileName} pomyślnie, temperatura wynosi: ${temperature}`,
			}),
		};
	} catch (error) {
		return {
			statusCode: 500,
			body: JSON.stringify({ error: error.message }),
		};
	}
};
