import { S3Client, ListObjectsV2Command, GetObjectCommand} from "@aws-sdk/client-s3";

const s3 = new S3Client({});
const BUCKET_NAME = "temperature-measurements-iot-system";

export const handler = async (event) => {
  try {
    const listCommand = new ListObjectsV2Command({
      Bucket: BUCKET_NAME,
      Prefix: "measurements"
    });
    const listResponse = await s3.send(listCommand);

    if(!listResponse.Contents || listResponse.Contents.length === 0) {
      return {
        statusCode: 404,
        body: JSON.stringify({message: "Brak pomiarów w bazie danych"})
      };
    }

    const latestFile = listResponse.Contents.reduce((prev, current) => {
      return prev.LastModified > current.LastModified ? prev : current;
    });

    const getResponse = await s3.send(new GetObjectCommand({
      Bucket: BUCKET_NAME,
      Key: latestFile.Key
    }));

    const bodyContents = await getResponse.Body.transformToString();

    return {
      statusCode: 200,
      body: bodyContents
    };

  } catch (error) {
    return {
      statusCode: 500,
      body: JSON.stringify({message: "Wystąpił błąd podczas pobierania danych"}),
      error: JSON.stringify({error: error.message})
    };
  }
};
